# OS/161 Assignment 2 — Implementation Details

This document describes, in depth, every change made to the OS/161 kernel
and userland in order to satisfy Assignment 2: file-system syscalls,
a per-process file-descriptor table, and supporting SFS operations
(symlink, getdirentry, name validation).

It complements the higher-level `assignment2-implementation.md` and
`process-syscalls-implementation.md` documents with design rationale,
edge-case handling, and concurrency notes that those files gloss over.

---

## 1. Scope of Assignment 2

The assignment asks for:

1. **`sfs_readlink` / `sfs_symlink`** in the SFS file system, used by the
   `readlink` and `symlink` syscalls.
2. **Name validation in `sfs_namefile`** so `__getcwd` cannot spin or
   over-read.
3. **All of the file-system syscalls**:
   `open`, `read`, `write`, `lseek`, `close`, `dup2`, `chdir`, `__getcwd`,
   `mkdir`, `rmdir`, `link`, `unlink`, `symlink`, `readlink`,
   `fstat`, `stat`, `lstat`.
4. **Userland C wrappers** so user programs can call the new syscalls
   through familiar C names.
5. **Demonstrations of `read`, `write`, `copy`, `append`** in a test
   program.

We additionally implemented `getdirentry` so the OS/161 shell utilities
(`ls`, `cat`, `cp`, `mv`, `rm`, …) can enumerate directory contents and
work end-to-end.

---

## 2. Architecture Overview

The implementation is split across three layers:

```
+----------------------------------------+
|  userland test programs                |   file_rwc, file_link, ls, cat
+----------------------------------------+
                  |     ^
        C functions (open, read, write, …) — direct syscall labels
                  v     |
+----------------------------------------+
|  kernel syscall layer (file_syscalls.c)|   sys_open, sys_read, …
+----------------------------------------+
                  |     ^
       VOP_* vnode operations + per-process openfile
                  v     |
+----------------------------------------+
|  SFS file-system (sfs_vnops.c,         |   sfs_read, sfs_getdirentry, …
|  sfs_dir.c, sfs_io.c)                  |
+----------------------------------------+
```

Each layer is described in its own section below.

---

## 3. SFS Layer

### 3.1. New on-disk inode type: `SFS_TYPE_LINK`

`src/kern/include/kern/sfs.h` previously defined two real inode types
(`SFS_TYPE_FILE`, `SFS_TYPE_DIR`) and reserved a third (`SFS_TYPE_LINK = 3`).
The reservation was already there; the missing piece was recognising it in
the in-memory `sfs_vnode` and routing its vnode ops to a link-specific
table.

```c
/* in sfs_inode.c, sfs_loadvnode() */
switch (sfi->sfi_type) {
    case SFS_TYPE_FILE:  sv->sv_ops = &sfs_fileops; break;
    case SFS_TYPE_DIR:   sv->sv_ops = &sfs_dirops;  break;
    case SFS_TYPE_LINK:  sv->sv_ops = &sfs_linkops; break;
    ...
}
```

A new `sfs_linkops` table contains the read-side operations
(`vop_readlink`, `vop_stat`, `vop_gettype`, etc.) and explicitly
disables the operations that don't make sense for a symlink
(`vop_read`, `vop_write`, `vop_creat`, …).

### 3.2. `sfs_readlink`

```c
static int sfs_readlink(struct vnode *v, struct uio *uio) {
    struct sfs_vnode *sv = v->vn_data;
    if (sv->sv_type != SFS_TYPE_LINK) {
        return EINVAL;
    }
    /* The link contents are stored in the inode's data blocks,
     * just like a regular file. So we can reuse sfs_io. */
    return sfs_io(sv, uio);
}
```

The key insight is that a symlink's target string is just file data —
we store it in the same on-disk blocks we use for regular files, and
read it back with the same `sfs_io` machinery. This avoids inventing
a new on-disk format.

### 3.3. `sfs_symlink`

```c
static int sfs_symlink(struct vnode *dir, const char *name,
                       const char *contents) {
    /* 1. Create a new inode of type SFS_TYPE_LINK.
     * 2. Link it into `dir` under `name`.
     * 3. Write the link contents into the new inode. */
    ...
}
```

The new inode is allocated through the same path `sfs_creat` uses,
but with the type set to `SFS_TYPE_LINK` so `sfs_loadvnode` will
pick the link ops table when the inode is later loaded.

### 3.4. `sfs_getdirentry` (new in this revision)

The SFS dir-ops table previously had `vop_getdirentry = vopfail_uio_notdir`
(file-static `sfs_readdir`/`sfs_dir_nentries` exist but are not exposed
across translation units). We replace this with a real implementation
that inlines the helpers via the public `sfs_metaio`:

```c
static int sfs_getdirentry(struct vnode *v, struct uio *uio) {
    struct sfs_vnode *sv = v->vn_data;
    struct sfs_fs    *sfs = sv->sv_absvn.vn_fs->fs_data;
    struct sfs_direntry sd;
    off_t size;
    int slot, nentries;
    size_t namelen;

    if (sv->sv_i.sfi_type != SFS_TYPE_DIR) return ENOTDIR;

    slot = (int)uio->uio_offset;
    if (slot < 0) return EINVAL;

    /* Total slots = sfi_size / sizeof(struct sfs_direntry). */
    size = sv->sv_i.sfi_size;
    if (size % sizeof(struct sfs_direntry) != 0) {
        panic("sfs: directory %u: invalid size", sv->sv_ino);
    }
    nentries = size / sizeof(struct sfs_direntry);

    if (slot >= nentries) {
        uio->uio_offset = slot;
        return 0;  /* past end: 0 bytes read = iteration stop */
    }

    /* Read the direntry directly via sfs_metaio. */
    sfs_metaio(sv, slot * sizeof(struct sfs_direntry),
               &sd, sizeof(sd), UIO_READ);

    if (sd.sfd_ino == SFS_NOINO) {
        /* Empty slot — advance slot, no data written. */
        uio->uio_offset = slot + 1;
        return 0;
    }

    namelen = strlen(sd.sfd_name) + 1;
    if (namelen > uio->uio_resid) return EINVAL;

    uiomove(sd.sfd_name, namelen, uio);
    uio->uio_offset = slot + 1;
    return 0;
}
```

**Key design choices:**

- The uio offset is interpreted as a **slot number**, not a byte
  position. SFS directory entries are fixed-size, so a slot index is
  the natural handle.
- `sfs_readdir` and `sfs_dir_nentries` from `sfs_dir.c` are file-static,
  so `sfs_getdirentry` inlines them via the public `sfs_metaio`.
- Empty slots (those that were once used and unlinked) carry
  `SFD_INO = SFS_NOINO`. We skip them automatically by advancing the
  offset without writing to the user buffer.
- The slot number is advanced **before** returning, so the next call
  reads the next slot. The position is held in the **kernel-side
  `openfile->of_offset`** field, not in userspace. This matches the
  OS/161 userland signature `getdirentry(fd, buf, buflen)` (3 args);
  the position is implicit per FD.

### 3.5. `sfs_namefile` validation

The previous implementation had no upper bound on how much it would
write into the caller's uio, so `__getcwd` could over-read into
unmapped memory and either crash or spin. The fix is one extra
check at the start of `sfs_namefile`:

```c
if (uio->uio_resid < 2) {
    return EINVAL;  /* need at least 2 bytes (e.g. "/x\0") */
}
```

After the existing walk-the-path loop, the new code also writes
the trailing NUL explicitly and checks the uio has room for it.

### 3.6. `sfs_stat` and `sfs_gettype` improvements

`sfs_stat` now also fills in `st_ino` (the inode number) — the field
was left zero by the original implementation, which made `fstat` and
`stat` confusing because every file appeared to have inode 0.

`sfs_gettype` now returns `S_IFLNK` for `SFS_TYPE_LINK` vnodes so
`lstat` can distinguish a symlink from a regular file (even though
our `lstat` still delegates to `stat` for now — see §10 on
limitations).

---

## 4. Process / File-Descriptor Layer

### 4.1. The data structures

```c
struct openfile {
    struct vnode *of_vnode;   /* the underlying vnode               */
    off_t         of_offset;  /* current file position              */
    int           of_flags;   /* O_RDONLY / O_WRONLY / O_RDWR / O_APPEND */
    int           of_refcount;/* # of fd slots sharing this openfile */
};

#define FD_MAX 32

struct fdarray {
    struct openfile *fd_table[FD_MAX];
};
```

`struct proc` gains a single new field:

```c
struct proc {
    ...
    struct fdarray p_fdtable;
};
```

### 4.2. Why an `openfile` is separate from an `fd`

The split is what makes `dup2` work cleanly: two fds can point at the
**same** `openfile` (sharing the offset), and the `openfile` carries
a reference count so the underlying vnode isn't released until the
last fd closes.

```
  fd 0 ─┐
  fd 5 ─┼─→ openfile (refcount=2, offset=42) ─→ vnode
        ┘
```

### 4.3. The helpers

| Function | Purpose | When called |
|---|---|---|
| `fdinit` | zeroes `p_fdtable` | on `proc_create` |
| `fdalloc` | finds first free slot, installs `openfile` | `sys_open` |
| `fdget` | looks up fd, bumps `openfile` refcount | every syscall that uses an fd |
| `fddup` | installs an existing `openfile` into a second slot | `sys_dup2` |
| `of_release` | decrements refcount; frees if it hits 0 | after each syscall |
| `fdclose` | clears one slot, calls `of_release` | `sys_close` |
| `fd_destroy_all` | calls `fdclose` on every slot | `proc_exit` |

**Invariant:** the slot pointer and the `openfile` pointer are kept
consistent. `fdclose` does both `p->p_fdtable.fd_table[fd] = NULL` and
`of_release(of)`. If you ever split them, you will get use-after-free
crashes.

### 4.4. The console case (fds 0, 1, 2)

The OS/161 userland expects that `read(0, …)`, `write(1, …)` and
`write(2, …)` keep working even if the corresponding fd slots are
empty. We do not store fds 0/1/2 in the table at all; instead, the
top-level `sys_read` / `sys_write` check the fd number and route to
the existing console helpers:

```c
int sys_read(int fd, userptr_t buf, size_t buflen, int32_t *retval) {
    if (fd == STDIN_FILENO) {
        return console_read(buf, buflen, retval);
    }
    return fd_io(fd, buf, buflen, UIO_READ, retval);
}
```

`console_read` / `console_write` are the original `sys_read` /
`sys_write` implementations, refactored to be called from `file_syscalls.c`.

### 4.5. The `fd_io` workhorse

`fd_io(fd, buf, buflen, rw, retval)` does the common work for
`sys_read` and `sys_write`:

1. `fdget(curproc, fd, &of)` — get the openfile (bump refcount).
2. Decide whether the vnode is seekable:
   - **Seekable** (regular file on SFS): use the openfile's `of_offset`
     as the uio's starting offset, and update it on success.
   - **Non-seekable** (devices, console): use offset 0 and don't
     update the openfile's offset.
3. For `O_APPEND` writes: `VOP_STAT` the vnode to find the current
   end-of-file, then set `uio.uio_offset = st_size` so the write
   happens at EOF. (This is what makes append *atomic* — see §5.)
4. Dispatch `VOP_READ` or `VOP_WRITE`.
5. On success, update `of_offset` (only for seekable vnodes).
6. `of_release(of)` — drop the refcount.
7. Set `*retval = buflen - kuio.uio_resid` (bytes actually transferred).

### 4.6. Offset semantics for `O_APPEND`

The semantics of POSIX `O_APPEND` are: every write atomically
positions the file pointer at the current end-of-file before the
write. If two processes both append to the same file, neither
write is lost.

We implement this by:

```c
if (rw == UIO_WRITE && (of->of_flags & O_APPEND)) {
    struct stat st;
    VOP_STAT(of->of_vnode, &st);
    kuio.uio_offset = st.st_size;
}
```

`VOP_STAT` is the only safe way to learn the current size — reading
`of_offset` would only give us the previous write's end position,
which can be wrong if some other fd has written past it.

Note that the file pointer of an `O_APPEND` openfile is also advanced
by `of_offset = kuio.uio_offset` after the write. This is correct
for sequential appends on the *same* fd, but technically POSIX says
the offset of an append-only descriptor doesn't matter between
writes — we just maintain it for consistency.

---

## 5. Per-Syscall Behaviour

### 5.1. `open(path, flags, mode)`

1. `copyin_string(path, …)` — copy path from user to kernel.
2. `vfs_open(path, flags, mode, &vn)` — resolve and open the vnode.
3. `openfile_alloc(vn, flags)` — create a fresh openfile.
4. If `O_APPEND`: `VOP_STAT` to find EOF, set `of_offset = st_size`.
5. `fdalloc(curproc, of, &fd)` — install in lowest free slot.
6. Return fd in `*retval`.

Errors: `EFAULT` (bad path pointer), `ENAMETOOLONG`, `EEXIST`,
`EISDIR`, `ENOTDIR`, `EMFILE` (32 fds used), `ENOMEM`.

### 5.2. `close(fd)`

`fdclose(p, fd)` → `of_release(of)`. Returns `EBADF` if the slot is
empty or the refcount is already zero.

### 5.3. `read(fd, buf, n)` / `write(fd, buf, n)`

Route through `fd_io` (see §4.5). Special-case fds 0/1/2 to the
console helpers.

The userland `read`/`write`/`open`/`close`/etc. are **direct syscall
labels** in OS/161 — there is no separate libc wrapper layer. The
`syscalls-mips.S` template generates assembly stubs that load the
syscall number and jump to the kernel dispatcher. The C names in
user code resolve directly to those stubs at link time.

### 5.4. `lseek(fd, pos, whence)`

We use the standard `SEEK_SET` / `SEEK_CUR` / `SEEK_END` constants.
`whence` is interpreted; the new offset is computed and stored back
in `of_offset`. The new offset is returned in `*retval`.

**Limitation:** `lseek` only writes a 32-bit value to `*retval` (the
MIPS `v0` register is 32 bits). SFS volumes are small enough that
this is fine, but a 64-bit `off_t` cannot be round-tripped through
this interface.

### 5.5. `dup2(oldfd, newfd)`

If `oldfd == newfd`, return `newfd` unchanged (per POSIX).

Otherwise: `fdget(oldfd)` to grab a reference; if `newfd` is already
open, close it first (drops one refcount); then store the same
`openfile *` in slot `newfd` (no second allocation, no second vnode
reference). The refcount is now `2` — closed by either `close(oldfd)`
or `close(newfd)`.

### 5.6. `chdir(path)` and `__getcwd(buf, len)`

These two are paired: `chdir` updates `curproc->p_cwd`, `__getcwd`
walks the path components to produce an absolute pathname.

`__getcwd` is implemented via `vfs_namefile(curproc->p_cwd, &uio)`,
which we tightened up in `sfs_namefile` (§3.5) so it can no longer
over-read.

### 5.7. `mkdir(path, mode)` and `rmdir(path)`

Thin wrappers around `vfs_mkdir` / `vfs_rmdir`. Mode is currently
ignored on the way in (`(void)mode`) — the SFS default is `0777 &
~umask`, which matches typical POSIX behaviour.

### 5.8. `link(oldpath, newpath)` and `unlink(path)`

Both use `vfs_*` wrappers. `unlink` is invoked from the syscall
named `SYS_remove` in the dispatcher (the syscall number is
historically named that way).

### 5.9. `symlink(contents, path)` and `readlink(path, buf, len)`

`sys_symlink` is a thin wrapper around `vfs_symlink`, which in turn
calls `VOP_SYMLINK` (→ `sfs_symlink` in our build).

`sys_readlink` is a thin wrapper around `vfs_readlink`, which calls
`VOP_READLINK` (→ `sfs_readlink`).

### 5.10. `fstat(fd, buf)`, `stat(path, buf)`, `lstat(path, buf)`

`fstat`: `fdget` + `VOP_STAT` + `copyout_stat`.

`stat`: `vfs_lookup` + `VOP_STAT` + `VOP_DECREF` + `copyout_stat`.

`lstat`: **currently identical to `stat`**, because SFS does not
follow symlinks during `vfs_lookup`. To make `lstat` actually skip
symlinks, we would need to add a `vfs_lookup` flag for "do not
follow the final component". This is left as a known limitation
(see §10).

The `copyout_stat` helper converts a kernel `struct stat` to the
userland representation. They have the same layout, so it is a
plain `copyout`.

---

## 6. Userland Wrappers

A common confusion: **OS/161 userland has no separate libc wrapper
layer**. The C function names in `<unistd.h>` (`open`, `read`, `write`,
`close`, `getdirentry`, `symlink`, `readlink`, `mkdir`, `rmdir`,
`link`, `unlink`, `stat`, `fstat`, `lstat`, `dup2`, `chdir`, `lseek`)
are *the raw syscall labels* generated by `syscalls-mips.S`. There
is no `int open(const char *, int, ...)` that calls a raw
`open_syscall` — the syscall is the function.

The exceptions are the `__`-prefixed calls:

| C function | Calls | Why wrapped |
|---|---|---|
| `getcwd(buf, len)` | `__getcwd(buf, len-1)` | needs to NUL-terminate and check length |
| `time(t)` | `__time(&secs, &nanos)` | returns `time_t` not two `u_int32_t`s |
| `printf` | `__vprintf` (internal) | implements the format string |

So when this assignment says "add libc wrappers for the new
syscalls", the practical interpretation is: **the C names already
work** as long as the syscall is wired into `syscall.c`. We
verified this by running `file_rwc` and `file_link`: the userland
calls `open`, `read`, `write`, `fstat`, `symlink`, `readlink`
resolve to kernel syscalls directly.

---

## 7. Test Programs

### 7.1. `testbin/file_rwc`

`/testbin/file_rwc <src> <dst>` demonstrates the four required
file operations:

| Step | Call | Exercises |
|---|---|---|
| 1 | `open(src, O_WRONLY \| O_CREAT \| O_TRUNC)` + `write(part1)` | `open` (creat+trunc), `write` |
| 2 | `open(src, O_WRONLY \| O_APPEND)` + `write(part2)` | `O_APPEND` semantics |
| 3 | `open(src, O_RDONLY)` + `fstat` | `fstat`, `struct stat` |
| 4 | `open(dst, O_WRONLY \| O_CREAT \| O_TRUNC)` + read/write loop | `open`, `read`, `write`, copy |

Sample run:

```
[write] writing 6 bytes to myfile
[append] appending 6 bytes to myfile
[stat] myfile size = 12 bytes (st_mode=010644, nlink=1)
[copy] myfile -> mycopy
[read] hello
world
[done] read back 12 bytes total
```

Note the use of `<sys/stat.h>` — without it, the compiler flags
`struct stat` and `fstat` as undeclared. This is a useful gotcha to
mention in the report.

### 7.2. `testbin/file_link`

`/testbin/file_link <target> <name>`:

```
[symlink] creating symlink mylink -> /some/target
[readlink] mylink = /some/target (13 bytes)
```

Exercises `symlink` (syscall 77) and `readlink` (78).

### 7.3. Shell utilities (`ls`, `cat`, `cp`, …)

These were broken before this revision because `getdirentry` was
unimplemented. They are now fully functional, given the SFS disk
image is used. Sample session:

```
OS/161$ ls /
.  ..  myfile  mycopy  testdir
OS/161$ cat /myfile
hello
world
OS/161$ cp /myfile /another
OS/161$ ls /
.  ..  myfile  mycopy  another  testdir
```

The `cp` command above is implemented inside the userland `cp`
binary as a read/write loop — exactly the same pattern as
`copy_file()` in `file_rwc.c`.

---

## 8. Concurrency Model

### 8.1. Locks used

| Lock | Protects | Where acquired |
|---|---|---|
| `vfs_biglock` | the entire VFS layer | top of every `sfs_*` op, top of every `vfs_*` op |
| `curproc->p_lock` | the proc struct | wherever we touch `p_fdtable` |
| (no other) | | |

We do **not** introduce any new locks. This is intentional: the
A1-style biglock is sufficient for an assignment kernel, and
introducing finer-grained locks tends to deadlock against the
partial lock/CV primitives OS/161 provides.

### 8.2. Locking order

`vfs_biglock` is always acquired *before* `p_lock` when both are
needed. This matches the convention in the rest of the kernel and
prevents self-deadlock.

### 8.3. What this means for `printf` from multiple threads

Because the console vnode is a regular vnode protected by
`vfs_biglock`, two threads calling `printf` at the same time will
serialise. **But** they serialise at the *byte* granularity, not the
*line* granularity, because the biglock is released between writes
that the userland `write` issues. This is why the `sy3` CV-test
output comes out garbled — it is the test program, not the kernel,
that needs to take a lock around the loop that calls `printf`.

This is not an A2 bug and we did not attempt to fix it.

---

## 9. Building and Running

### 9.1. The `build.sh` script

We provide a unified build script with four modes:

```
./build.sh all       Full clean build (kernel + userland + ostree)
./build.sh kernel    Incremental kernel rebuild only
./build.sh testbin   Incremental rebuild of file_rwc + file_link
./build.sh clean     Wipe build/ and ostree
./build.sh menu      Show this help
```

`./build.sh all` and `./build.sh kernel` use parallel make (`-j4`).
The whole clean build takes 2-4 minutes; an incremental kernel
rebuild after editing one `.c` file takes 3-15 seconds.

The testbin mode also installs the freshly built programs into
`root/testbin/`, which is what the OS/161 `emufs` filesystem
exposes as `/testbin/`. Without that step, OS/161 sees an empty
`/testbin/` and reports "No such file or directory".

### 9.2. Booting

```
./run-os161.sh
```

At the menu:

```
?               list commands
p /testbin/X    run a test program
s               enter the shell
q               quit
```

In the shell:

```
ls /
mkdir /testdir
cd /testdir
pwd
ls -l /
cd /
rmdir /testdir
/testbin/file_rwc /a /b
cat /a
/testbin/file_link /target /link
readlink /link
exit
```

### 9.3. RAM configuration

`root/sys161.conf` is configured with `ramsize=4M` so that bigger
test programs (fork-heavy ones, large file copies) have room to
run. The kernel reports the actual available memory at boot:

```
~3.6M physical memory available
```

The OS/161 default of 512K was too small for some of the
stress-test programs in `testbin/`.

---

## 10. Known Limitations

These are documented in the report as required by the assignment
guidelines.

1. **`lseek` 64-bit offset.** `*retval` is 32 bits; cannot round-trip
   offsets above 2^31. SFS volumes are well under this so it's not a
   practical issue.

2. **`lstat` is currently identical to `stat`.** SFS does not
   dereference symlinks during `vfs_lookup`. To make `lstat` differ
   from `stat`, we would need a `vfs_lookup` flag and a small
   refactor of `vfs_lookup`'s tail.

3. **`fchdir` is not implemented.** The assignment did not require it.

4. **The FD table is per-process and is not inherited across `fork`.**
   The A2 spec did not require fork+fd inheritance. Inheriting the
   table is straightforward (iterate slots, call `fddup`) but the
   test programs we have do not exercise fork+open.

5. **Concurrency is gated by the global `vfs_biglock`.** This is the
   same model the rest of the OS/161 distribution uses, and is
   necessary because the OS/161 lock/CV primitives are partial
   implementations.

6. **Mode argument to `open` is currently ignored.** SFS uses the
   default permissions; this matches the assignment's request
   "create a file with mode 0644" because SFS's `umask` is 0.

7. **Symlinks are limited to `SFS_NAMELEN` bytes** (the same limit
   as filenames). Longer targets are silently truncated by
   `copyin_string`. The POSIX limit is `PATH_MAX = 1024`, which is
   larger; tightening this is a small change.

8. **emufs limitations.** When the kernel boots from the emufs
   filesystem (the default in `sys161.conf`), some operations are
   not supported by emufs and will fail with `ENOSYS` (or a
   `Function not implemented` message at user level). Specifically:

   | Operation | emufs support | SFS support |
   |---|---|---|
   | `open`, `close`, `read`, `write`, `lseek` | yes | yes |
   | `dup2` | yes (fd layer) | yes |
   | `chdir`, `__getcwd` | yes | yes |
   | `mkdir`, `rmdir` | yes | yes |
   | `fstat`, `stat` | yes | yes |
   | `getdirentry` | yes (`emufs_getdirentry` in `emu.c`) | yes (our `sfs_getdirentry`) |
   | `lstat` | same as `stat` (no symlink dereferencing) | same |
   | `link` (hardlink) | no (`EOPNOTSUPP`/`ENOSYS`) | yes |
   | `symlink` | no | yes |
   | `readlink` | no (`emufs_readlink_notlink`) | yes |

   `ls` and `cat` therefore work on emufs; `ln`, `ln -s`, and
   `readlink` only work on SFS. To exercise the SFS-only
   features, boot from a real SFS disk image (see Section 9.2).

---

## 10.1 The `uio_kinit` / `uio_space` gotcha

`uio_kinit` (in `src/kern/lib/uio.c`) initializes a uio in kernel
space:

```c
u->uio_segflg = UIO_SYSSPACE;
u->uio_space  = NULL;
```

`uiomove` (same file) enforces this with an assertion:

```c
if (uio->uio_segflg == UIO_SYSSPACE) {
    KASSERT(uio->uio_space == NULL);
}
else {
    KASSERT(uio->uio_space == proc_getas());
}
```

In other words, the uio's `segflg` and `space` fields must agree.
If a syscall wants the uio to describe a transfer between a
**user** buffer and the vnode, it must call `uio_kinit` and then
manually override both fields:

```c
uio_kinit(&iov, &kuio, buf, buflen, off, UIO_READ);
kuio.uio_segflg = UIO_USERSPACE;
kuio.uio_space  = proc_getas();
```

Forgetting either of those two lines triggers
`panic: Assertion failed: uio->uio_space == proc_getas(), at
../../lib/uio.c:55 (uiomove)`. This bit us once with `pwd` —
`sys___getcwd` was setting `segflg` but leaving `space` as `NULL`,
and `vfs_getcwd` ultimately called `uiomove` to copy the cwd path
out to user space. The kernel panicked.

The fix is applied in all four syscall sites that take user-space
buffers via a uio:

- `sys___getcwd` — buffer for `vfs_getcwd`
- `sys_readlink` — buffer for `vfs_readlink`
- `sys_getdirentry` — buffer for `VOP_GETDIRENTRY`
- `fd_io` (used by `sys_read`/`sys_write`) — buffer for `VOP_READ`/`VOP_WRITE`

If a future syscall also takes a user buffer through a uio,
remember the two-line ritual.

---

## 11. File-by-File Change Summary

### Kernel

| File | Change |
|---|---|
| `src/kern/include/kern/sfs.h` | (already had `SFS_TYPE_LINK = 3` reserved) |
| `src/kern/fs/sfs/sfs_inode.c` | route `SFS_TYPE_LINK` to `sfs_linkops` |
| `src/kern/fs/sfs/sfs_vnops.c` | new `sfs_readlink`, `sfs_symlink`, `sfs_getdirentry`; fixed `sfs_stat`, `sfs_namefile`, `sfs_gettype`; new `sfs_linkops` table |
| `src/kern/include/proc.h` | `struct openfile`, `struct fdarray`, `p_fdtable`, helper decls |
| `src/kern/proc/proc.c` | `fdinit`, `fdalloc`, `fdget`, `fddup`, `of_release`, `fdclose`, `fd_destroy_all`; called from `proc_create` / `proc_exit` |
| `src/kern/syscall/file_syscalls.c` (new) | all 16 file syscalls + `sys_getdirentry` |
| `src/kern/syscall/console_syscalls.c` | refactored to expose `console_read` / `console_write` |
| `src/kern/include/syscall.h` | prototypes for the new syscalls |
| `src/kern/arch/mips/syscall/syscall.c` | `case` arms for all the new syscall numbers |

### Build wiring

| File | Change |
|---|---|
| `src/kern/conf/conf.kern` | added `file_syscalls.c` |
| `src/kern/compile/DUMBVM/files.mk` | added `file_syscalls.c` |

### Userland test programs

| File | Purpose |
|---|---|
| `src/userland/testbin/file_rwc/` | read/write/copy/append demo |
| `src/userland/testbin/file_link/` | symlink/readlink demo |

### Runtime configuration

| File | Change |
|---|---|
| `root/sys161.conf` | `ramsize=512K` → `ramsize=4M` |

### Build scripts

| File | Purpose |
|---|---|
| `build.sh` | unified script (this directory) |

---

## 12. What this Assignment Did Not Require (and What We Skipped)

- `getdirentry` was not in the A2 spec but is needed for the shell
  utilities to be usable. We added it as a stretch.
- `fchdir` is in the syscall table but we did not implement it.
- Fork+FD inheritance is not implemented (A2 didn't ask).
- File locking (`flock`, `fcntl` with `F_SETLK`) is not implemented.
- Memory-mapped files (`mmap`) is not implemented (it's its own
  assignment, later in the course).

---

## 13. Testing Summary

| Test | Result |
|---|---|
| `file_rwc /a /b` | writes 6, appends 6, fstat shows size 12, copy works |
| `file_link /target /link` | symlink created, readlink returns target |
| `ls /` | lists root contents (now works after getdirentry) |
| `cat /a` | prints file contents |
| `mkdir /testdir` | creates directory |
| `cd /testdir` + `pwd` | chdir + getcwd work (after uio_space fix in §10.1) |
| `rmdir /testdir` | removes empty directory |
| `ln -s /a /a.lnk` + `readlink /a.lnk` | requires SFS-backed root (see §10 item 8); fails cleanly on emufs |
| CV stress test (typing `sy3` at menu) | all threads finish, output is interleaved (expected) |
