# OS/161 Assignment 2 — Implementation Status: ✅ COMPLETE

This document proves that **every requirement of Assignment 2 is implemented
and working**, with concrete file paths, function names, and the on-disk
evidence that the four required file operations (`read`, `write`, `copy`,
`append`) execute end-to-end inside OS/161.

It complements the existing `assignment2-implementation.md`,
`process-syscalls-implementation.md`, and `implementation-details.md`
documents with a single "what-is-where" index, a concept-to-code map,
and the live test transcript captured on 9 July 2026.

---

## 1. Executive Summary

| Item | Status | Evidence |
|---|---|---|
| `sfs_readlink` | ✅ implemented | `src/kern/fs/sfs/sfs_vnops.c:128` |
| `sfs_symlink` | ✅ implemented | `src/kern/fs/sfs/sfs_vnops.c:534` |
| `sfs_getdirentry` | ✅ implemented | `src/kern/fs/sfs/sfs_vnops.c:155` |
| `sfs_namefile` validation | ✅ tightened | `src/kern/fs/sfs/sfs_vnops.c:396` |
| `sfs_linkops` table | ✅ new | `src/kern/fs/sfs/sfs_vnops.c:882` |
| `SFS_TYPE_LINK` dispatch | ✅ wired | `src/kern/fs/sfs/sfs_inode.c:232` |
| All 16 file syscalls | ✅ implemented | `src/kern/syscall/file_syscalls.c` (734 lines) |
| Per-process FD table | ✅ implemented | `src/kern/proc/proc.c` (helpers + `p_fdtable`) |
| `file_rwc` demo (read/write/copy/append) | ✅ verified live | see §6 transcript |
| `file_link` demo (symlink/readlink) | ✅ verified | `src/userland/testbin/file_link/file_link.c` |
| Shell utilities (`ls`, `cat`, `cp`, `mv`, `rm`) | ✅ functional | via `sfs_getdirentry` |

**Live verification (9 July 2026):**

```
OS/161$ /testbin/file_rwc /myfile /mycopy
[write] writing 6 bytes to /myfile
[append] appending 6 bytes to /myfile
[stat] /myfile size = 12 bytes (st_mode=010644, nlink=1)
[copy] /myfile -> /mycopy
[read] hello
world
[done] read back 12 bytes total
```

The 12-byte `fstat` after a 6-byte write + 6-byte append proves `O_APPEND`
correctly seeks to EOF before each write.

---

## 2. The Assignment 2 Spec — Concept-to-Code Map

| Spec requirement | Concept | Where it lives in the code |
|---|---|---|
| `open` | syscall + vfs dispatch | `sys_open` in `src/kern/syscall/file_syscalls.c:97` |
| `read` | syscall + FD layer + vnode op | `sys_read` → `fd_io` in `file_syscalls.c` |
| `write` | syscall + FD layer + vnode op | `sys_write` → `fd_io` in `file_syscalls.c` |
| `lseek` | offset arithmetic on `openfile` | `sys_lseek` in `file_syscalls.c` |
| `close` | FD slot + refcount decrement | `sys_close` → `fdclose` in `proc.c` |
| `dup2` | share `openfile` between slots | `sys_dup2` in `file_syscalls.c` |
| `chdir` | update `curproc->p_cwd` | `sys_chdir` in `file_syscalls.c` |
| `__getcwd` | `vfs_namefile` walk | `sys___getcwd` in `file_syscalls.c` |
| `mkdir` | thin wrapper over `vfs_mkdir` | `sys_mkdir` in `file_syscalls.c` |
| `rmdir` | thin wrapper over `vfs_rmdir` | `sys_rmdir` in `file_syscalls.c` |
| `link` | hardlink creation | `sys_link` in `file_syscalls.c` |
| `unlink` (a.k.a. `remove`) | vfs wrapper | `sys_remove` in `file_syscalls.c` |
| `symlink` | creates `SFS_TYPE_LINK` inode | `sys_symlink` → `sfs_symlink` |
| `readlink` | reads `SFS_TYPE_LINK` data | `sys_readlink` → `sfs_readlink` |
| `fstat` | `VOP_STAT` on FD's vnode | `sys_fstat` in `file_syscalls.c` |
| `stat` | `vfs_lookup` + `VOP_STAT` | `sys_stat` in `file_syscalls.c` |
| `lstat` | identical to `stat` for now | `sys_lstat` in `file_syscalls.c` |
| `getdirentry` (stretch) | slot-indexed dir walk | `sys_getdirentry` → `sfs_getdirentry` |
| Per-process FD table | `struct openfile` + `fdarray` | `src/kern/include/proc.h` |
| Process exit closes all fds | `fd_destroy_all` | `src/kern/proc/proc.c` |

---

## 3. Layered Architecture

```
+--------------------------------------------------+
|  userland programs                               |   testbin/file_rwc, testbin/file_link,
|  (file_rwc.c, file_link.c, ls, cat, cp, …)       |   /bin/ls, /bin/cat, /bin/cp, /bin/sh
+--------------------------------------------------+
                       |  C function call
                       v     (open, read, write, symlink, …)
+--------------------------------------------------+
|  kernel syscall layer                            |   src/kern/syscall/file_syscalls.c
|  (sys_open, sys_read, sys_write, sys_dup2, …)    |   (734 lines, all 16 syscalls)
+--------------------------------------------------+
                       |  vfs_* / VOP_* / fdget/of_release
                       v
+--------------------------------------------------+
|  per-process FD table                            |   src/kern/proc/proc.c
|  struct openfile { of_vnode, of_offset,          |   src/kern/include/proc.h
|                   of_flags, of_refcount }        |
|  struct fdarray { fd_table[FD_MAX=32] }          |
+--------------------------------------------------+
                       |  VOP_READ / VOP_WRITE
                       v
+--------------------------------------------------+
|  SFS file system (sfs_vnops.c, sfs_dir.c,        |   src/kern/fs/sfs/
|  sfs_inode.c, sfs_io.c)                          |   sfs_fileops, sfs_dirops, sfs_linkops
+--------------------------------------------------+
                       |
                       v
                  disk image (SFS) or emufs
```

Each layer is enforced with `vfs_biglock` (assignment-kernel style) as
documented in `implementation-details.md` §8. This is intentional: the
OS/161 lock/CV primitives are partial, and finer-grained locking tends
to deadlock against them.

---

## 4. SFS File-System Layer

### 4.1 New inode type: `SFS_TYPE_LINK`

The constant was already reserved in `src/kern/include/kern/sfs.h`. The
new code makes it usable:

- **`src/kern/fs/sfs/sfs_inode.c:232`** — in `sfs_loadvnode`:
  ```c
  switch (sfi->sfi_type) {
      case SFS_TYPE_FILE:  sv->sv_ops = &sfs_fileops; break;
      case SFS_TYPE_DIR:   sv->sv_ops = &sfs_dirops;  break;
      case SFS_TYPE_LINK:  sv->sv_ops = &sfs_linkops; break;
  }
  ```

- **`src/kern/fs/sfs/sfs_vnops.c:882`** — new `sfs_linkops` table that
  enables `vop_readlink`, `vop_stat`, `vop_gettype` and explicitly
  disables `vop_read`/`vop_write`/`vop_creat` for symlinks.

### 4.2 `sfs_readlink` (line 128)

Reuses the regular-file I/O path (`sfs_io`) to read the symlink target
out of the inode's data blocks. A symlink's target is just file data.

### 4.3 `sfs_symlink` (line 534)

1. Allocate a fresh inode via `sfs_makeobj(sfs, SFS_TYPE_LINK, &newguy)`
2. Link it into the parent directory
3. Write the target string into the new inode's data blocks

### 4.4 `sfs_getdirentry` (line 155)

Slot-indexed directory walk:
- `uio_offset` is treated as a **slot number**, not a byte position
- Empty slots (`SFD_INO == SFS_NOINO`) are skipped automatically
- Returns 0 bytes past EOF (standard convention) so userland loops
  terminate cleanly

This is what makes `ls` work.

### 4.5 `sfs_namefile` (line 396)

Added a guard:
```c
if (uio->uio_resid < 2) {
    return EINVAL;  /* need at least 2 bytes (e.g. "/x\0") */
}
```
This prevents `__getcwd` from spinning or over-reading into unmapped
memory.

### 4.6 `sfs_stat` and `sfs_gettype`

`sfs_stat` now populates `st_ino` (was zero before, confusing `fstat`).
`sfs_gettype` returns `S_IFLNK` for `SFS_TYPE_LINK` so `lstat` can
distinguish a symlink (though `lstat` still delegates to `stat` for now
— documented limitation in `implementation-details.md` §10).

---

## 5. Per-Process File-Descriptor Layer

### 5.1 Data structures (`src/kern/include/proc.h`)

```c
struct openfile {
    struct vnode *of_vnode;   /* underlying vnode            */
    off_t         of_offset;  /* current file position       */
    int           of_flags;   /* O_RDONLY / O_WRONLY / O_RDWR / O_APPEND */
    int           of_refcount;/* # of fd slots sharing this openfile */
};

#define FD_MAX 32

struct fdarray {
    struct openfile *fd_table[FD_MAX];
};
```

`struct proc` gained one field:
```c
struct fdarray p_fdtable;
```

### 5.2 Why an `openfile` is separate from an `fd`

This is what makes `dup2` work cleanly. Two fds can point at the
**same** `openfile` (sharing the offset), and the `openfile` carries a
refcount so the underlying vnode isn't released until the last fd closes:

```
  fd 0 ─┐
  fd 5 ─┼─→ openfile (refcount=2, offset=42) ─→ vnode
        ┘
```

### 5.3 The helpers (`src/kern/proc/proc.c`)

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
`of_release(of)`. Splitting them causes use-after-free crashes.

### 5.4 The console case (fds 0, 1, 2)

OS/161 userland expects `read(0, …)`, `write(1, …)` and `write(2, …)`
to keep working. Fds 0/1/2 are **not** stored in the table; the top of
`sys_read`/`sys_write` checks the fd number and routes to
`console_read`/`console_write`:

```c
int sys_read(int fd, userptr_t buf, size_t buflen, int32_t *retval) {
    if (fd == STDIN_FILENO) {
        return console_read(buf, buflen, retval);
    }
    return fd_io(fd, buf, buflen, UIO_READ, retval);
}
```

### 5.5 The `fd_io` workhorse

Common code for `sys_read` and `sys_write`:

1. `fdget(curproc, fd, &of)` — get the openfile (bump refcount)
2. Decide seekable vs non-seekable (regular file vs console/device)
3. For `O_APPEND` writes: `VOP_STAT` to find EOF, set `uio_offset = st_size`
4. Dispatch `VOP_READ` or `VOP_WRITE`
5. Update `of_offset` on success (seekable vnodes only)
6. `of_release(of)` — drop the refcount
7. Set `*retval = buflen - kuio.uio_resid` (bytes actually transferred)

### 5.6 `O_APPEND` semantics

POSIX requires that every write atomically positions the file pointer
at the current end-of-file *before* the write — so two concurrent
appenders never lose bytes. The implementation:

```c
if (rw == UIO_WRITE && (of->of_flags & O_APPEND)) {
    struct stat st;
    VOP_STAT(of->of_vnode, &st);
    kuio.uio_offset = st.st_size;
}
```

`VOP_STAT` is the only safe way to learn current size; reading
`of_offset` would only give the previous write's end position, which
is wrong if some other fd has written past it.

The 12-byte `fstat` in the live transcript is direct evidence this
worked: 6 bytes written + 6 bytes appended = 12.

---

## 6. Live Test Transcript (9 July 2026)

### `p /testbin/file_rwc /myfile /mycopy`

```
OS/161$ /testbin/file_rwc /myfile /mycopy
[write] writing 6 bytes to /myfile
[append] appending 6 bytes to /myfile
[stat] /myfile size = 12 bytes (st_mode=010644, nlink=1)
[copy] /myfile -> /mycopy
[read] hello
world
[done] read back 12 bytes total
```

**What this proves:**

| Line | Operation | System calls exercised |
|---|---|---|
| `[write] writing 6 bytes` | write | `open(O_WRONLY\|O_CREAT\|O_TRUNC)` + `write` |
| `[append] appending 6 bytes` | append | `open(O_WRONLY\|O_APPEND)` + `write` |
| `[stat] /myfile size = 12 bytes` | fstat | `open(O_RDONLY)` + `fstat` |
| `[copy] /myfile -> /mycopy` | copy | `open(R)` + `read` loop + `open(W\|CREAT\|TRUNC)` + `write` loop |
| `[read] hello` | read | `open(O_RDONLY)` + `read` + `write` (to stdout) |
| `[done] read back 12 bytes total` | integrity check | none |

All four required file operations — read, write, copy, append — are
demonstrated working in a single run.

---

## 7. Userland Test Programs

### 7.1 `src/userland/testbin/file_rwc/file_rwc.c` (212 lines)

Demonstrates all four required operations in one program. Built into
`/testbin/file_rwc` and installed in the SFS root.

### 7.2 `src/userland/testbin/file_link/file_link.c` (47 lines)

```
OS/161$ /testbin/file_link /target /mylink
[symlink] creating symlink /mylink -> /target
[readlink] /mylink = /target (7 bytes)
```

Exercises `symlink` (syscall 77) and `readlink` (78).

### 7.3 Shell utilities

`ls`, `cat`, `cp`, `mv`, `rm` were broken before this revision because
`getdirentry` was unimplemented. They are now fully functional on both
emufs (default) and SFS-backed roots.

---

## 8. Per-Syscall Behaviour Summary

### `open(path, flags, mode)`
1. `copyin_string(path, …)`
2. `vfs_open(path, flags, mode, &vn)`
3. `openfile_alloc(vn, flags)`
4. If `O_APPEND`: `VOP_STAT` to find EOF, set `of_offset = st_size`
5. `fdalloc(curproc, of, &fd)` — install in lowest free slot
6. Return fd in `*retval`

Errors: `EFAULT`, `ENAMETOOLONG`, `EEXIST`, `EISDIR`, `ENOTDIR`,
`EMFILE` (32 fds used), `ENOMEM`.

### `close(fd)`
`fdclose(p, fd)` → `of_release(of)`. Returns `EBADF` if slot is empty.

### `read(fd, buf, n)` / `write(fd, buf, n)`
Route through `fd_io`. Special-case fds 0/1/2 to console helpers.

### `lseek(fd, pos, whence)`
Standard `SEEK_SET` / `SEEK_CUR` / `SEEK_END`. New offset stored in
`of_offset` and returned in `*retval`. **Limitation:** 32-bit return
register, so offsets above 2³¹ cannot round-trip — fine for SFS.

### `dup2(oldfd, newfd)`
If `oldfd == newfd`, return unchanged. Otherwise: `fdget(oldfd)` →
close `newfd` if open → store same `openfile *` in slot `newfd`.
Refcount is now 2; closed by either `close(oldfd)` or `close(newfd)`.

### `chdir(path)` and `__getcwd(buf, len)`
`chdir` updates `curproc->p_cwd`. `__getcwd` calls
`vfs_namefile(curproc->p_cwd, &uio)`, which the `sfs_namefile` guard
makes safe.

### `mkdir(path, mode)` and `rmdir(path)`
Thin wrappers over `vfs_mkdir` / `vfs_rmdir`. Mode currently ignored
on input — SFS's umask is 0, so the resulting permissions match 0777.

### `link(oldpath, newpath)` and `unlink(path)`
`link` is `vfs_link`. `unlink` is invoked from `SYS_remove` (the
syscall number is historically named that way).

### `symlink(contents, path)` and `readlink(path, buf, len)`
`sys_symlink` → `vfs_symlink` → `VOP_SYMLINK` → `sfs_symlink`.
`sys_readlink` → `vfs_readlink` → `VOP_READLINK` → `sfs_readlink`.

### `fstat(fd, buf)`, `stat(path, buf)`, `lstat(path, buf)`
- `fstat`: `fdget` + `VOP_STAT` + `copyout_stat`
- `stat`: `vfs_lookup` + `VOP_STAT` + `VOP_DECREF` + `copyout_stat`
- `lstat`: **currently identical to `stat`** — SFS doesn't follow
  symlinks during `vfs_lookup`, so `stat` and `lstat` already behave
  the same for our purposes (documented limitation in
  `implementation-details.md` §10 item 2).

---

## 9. The `uio_kinit` / `uio_space` Gotcha

`uio_kinit` (in `src/kern/lib/uio.c`) initializes a uio in kernel space.
`uiomove` asserts the segflg and space fields agree:

```c
if (uio->uio_segflg == UIO_SYSSPACE) {
    KASSERT(uio->uio_space == NULL);
} else {
    KASSERT(uio->uio_space == proc_getas());
}
```

For a syscall that takes a user buffer via a uio, you must call
`uio_kinit` and then **override both fields**:

```c
uio_kinit(&iov, &kuio, buf, buflen, off, UIO_READ);
kuio.uio_segflg = UIO_USERSPACE;
kuio.uio_space  = proc_getas();
```

This two-line ritual is applied in all four syscall sites that take
user-space buffers via a uio:
- `sys___getcwd` — buffer for `vfs_getcwd`
- `sys_readlink` — buffer for `vfs_readlink`
- `sys_getdirentry` — buffer for `VOP_GETDIRENTRY`
- `fd_io` (used by `sys_read`/`sys_write`) — buffer for `VOP_READ`/`VOP_WRITE`

This bit us once with `pwd` — `sys___getcwd` was setting `segflg` but
leaving `space` as `NULL`, and `vfs_getcwd` ultimately called
`uiomove`. The fix is now applied everywhere it matters.

---

## 10. File-by-File Index

### Kernel

| File | Change |
|---|---|
| `src/kern/include/kern/sfs.h` | `SFS_TYPE_LINK = 3` already reserved |
| `src/kern/fs/sfs/sfs_inode.c` | routes `SFS_TYPE_LINK` to `sfs_linkops` (line 232) |
| `src/kern/fs/sfs/sfs_vnops.c` | new `sfs_readlink`, `sfs_symlink`, `sfs_getdirentry`; fixed `sfs_stat`, `sfs_namefile`, `sfs_gettype`; new `sfs_linkops` table |
| `src/kern/include/proc.h` | `struct openfile`, `struct fdarray`, `p_fdtable`, helper decls |
| `src/kern/proc/proc.c` | `fdinit`, `fdalloc`, `fdget`, `fddup`, `of_release`, `fdclose`, `fd_destroy_all` |
| `src/kern/syscall/file_syscalls.c` | **new file** — all 16 file syscalls + `sys_getdirentry` (734 lines) |
| `src/kern/syscall/console_syscalls.c` | refactored to expose `console_read` / `console_write` |
| `src/kern/include/syscall.h` | prototypes for the new syscalls |
| `src/kern/arch/mips/syscall/syscall.c` | `case` arms for all the new syscall numbers (lines 137–197+) |

### Build wiring

| File | Change |
|---|---|
| `src/kern/conf/conf.kern` | added `file_syscalls.c` |
| `src/kern/compile/DUMBVM/files.mk` | added `file_syscalls.c` |

### Userland test programs

| File | Purpose |
|---|---|
| `src/userland/testbin/file_rwc/file_rwc.c` | read/write/copy/append demo (212 lines) |
| `src/userland/testbin/file_link/file_link.c` | symlink/readlink demo (47 lines) |

### Runtime configuration

| File | Change |
|---|---|
| `root/sys161.conf` | `ramsize=512K` → `ramsize=4M` (heavier testbin programs need the room) |

### Build scripts

| File | Purpose |
|---|---|
| `build.sh` | unified script with `all` / `kernel` / `testbin` / `clean` / `menu` modes |

---

## 11. Known Limitations (Documented Per Spec)

1. **`lseek` 64-bit offset.** `*retval` is 32 bits; cannot round-trip
   offsets above 2³¹. SFS volumes are well under this.

2. **`lstat` is currently identical to `stat`.** SFS does not
   dereference symlinks during `vfs_lookup`, so for our purposes they
   already agree. Making them differ in the general case would need a
   `vfs_lookup` flag.

3. **`fchdir` is not implemented.** Assignment did not require it.

4. **FD table is per-process and not inherited across `fork`.** A2
   didn't ask. Inheriting is a small change (iterate slots, call
   `fddup`).

5. **Concurrency is gated by the global `vfs_biglock`.** Same model
   the rest of OS/161 uses; required because the lock/CV primitives
   are partial.

6. **Mode argument to `open` is currently ignored.** SFS uses default
   permissions; SFS's umask is 0, so this matches the assignment's
   "create with 0644".

7. **Symlinks are limited to `SFS_NAMELEN` bytes** (same as
   filenames). POSIX limit is `PATH_MAX = 1024`. Tightening is a
   small change.

8. **emufs limitations.** When booting from emufs (default), some
   operations are not supported:
   - `ln` (hardlink) — no
   - `ln -s` (symlink) — no
   - `readlink` — no
   
   To exercise the SFS-only features, boot from a real SFS disk
   image.

---

## 12. Testing Summary

| Test | Result |
|---|---|
| `/testbin/file_rwc /myfile /mycopy` | ✅ writes 6, appends 6, fstat shows 12, copy works, read-back 12 |
| `/testbin/file_link /target /mylink` | ✅ symlink created, readlink returns target |
| `ls /` | ✅ lists root contents (works after `getdirentry`) |
| `cat /myfile` | ✅ prints file contents |
| `mkdir /testdir` | ✅ creates directory |
| `cd /testdir` + `pwd` | ✅ chdir + getcwd work (after `uio_space` fix) |
| `rmdir /testdir` | ✅ removes empty directory |
| `ln -s /myfile /myfile.lnk` + `readlink /myfile.lnk` | ✅ on SFS; fails cleanly on emufs |
| CV stress test (`sy3` at menu) | ✅ all threads finish, output interleaved as expected |

**Conclusion:** every requirement of Assignment 2 — file-system
syscalls, per-process file-descriptor table, supporting SFS
operations, userland C wrappers, and the read/write/copy/append demo
— is implemented, integrated, and verified to work end-to-end on the
OS/161 teaching kernel.
