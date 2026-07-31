# File Operations & Running Programs from the Menu — How They Work (In Detail)

This document explains two things end-to-end:

1. **How file operations are implemented** — the kernel file syscalls (`open`,
   `close`, `read`, `write`, `lseek`, `link`, `unlink`, `symlink`, `readlink`,
   `stat`, …) and the userland test programs that exercise them:
   - **`filetest`** — basic open/write/read/remove round-trip
   - **`file_rwc`** — write, read, append, and copy (read/write/create)
   - **`file_link`** — symlink + readlink
   - **`fileoperations`** — a multi-mode file utility (cat/write/append/copy)
   - **`pf`** (the menu's `printfile`) — dump a file to the console
2. **How you execute a program from the menu** — the `p` and `s` commands, and
   the `runprogram()` path that turns an ELF file into a running user process.

---

## 1. The big picture: three layers

```
   USERLAND test program            e.g. filetest.c, file_rwc.c, file_link.c
   (open/read/write/... libc calls)
              |
              |  MIPS syscall trap (system call number in v0)
              v
   KERNEL syscall layer             src/kern/syscall/file_syscalls.c
   (sys_open, sys_read, sys_write,  + the fd-table helpers in proc.c
    sys_link, sys_symlink, ...)
              |
              |  VOP_* / vfs_* calls
              v
   VFS + filesystem (SFS / emufs)   vnodes, the actual on-disk data
```

- **Userland** programs call ordinary C library functions (`open`, `write`, …).
  Those are thin wrappers that trap into the kernel.
- The **kernel syscall layer** (the file we implemented,
  [file_syscalls.c](../src/kern/syscall/file_syscalls.c)) validates arguments,
  copies data across the user/kernel boundary, tracks open files in a
  per-process **file-descriptor table**, and forwards the real work to the VFS.
- The **VFS** (`vfs_open`, `VOP_READ`, `vfs_link`, …) talks to the concrete
  filesystem (SFS on a simulated disk, or emufs on the host).

Files involved:

| File | Role |
|------|------|
| [src/kern/syscall/file_syscalls.c](../src/kern/syscall/file_syscalls.c) | All file-related syscalls. |
| [src/kern/proc/proc.c](../src/kern/proc/proc.c) | The fd-table helpers: `fdalloc`, `fdget`, `fddup`, `of_release`, `fdclose`. |
| [src/kern/include/proc.h](../src/kern/include/proc.h) | `struct openfile`, `struct fdarray`, `FD_MAX`. |
| [src/kern/syscall/runprogram.c](../src/kern/syscall/runprogram.c) | Loads and launches a user program. |
| [src/kern/main/menu.c](../src/kern/main/menu.c) | `p`, `s`, `pf` menu commands. |
| [src/kern/test/fstest.c](../src/kern/test/fstest.c) | `pf`/`printfile` and the `fs1`–`fs6` kernel tests. |
| `src/userland/testbin/{filetest,file_rwc,file_link,fileoperations}/` | The userland test programs. |

---

## 2. The core data structures

### 2.1 `struct openfile` — one open file

From [proc.h](../src/kern/include/proc.h):

```c
struct openfile {
	struct vnode *of_vnode;   /* the vnode for this open file            */
	off_t of_offset;          /* current file position (seek pointer)    */
	int of_flags;             /* open flags (O_RDONLY/O_WRONLY/O_RDWR/O_APPEND) */
	int of_refcount;          /* number of fd table entries pointing here */
};
```

An `openfile` is the kernel's record of one `open()`. It pairs a **vnode** (the
VFS handle to the actual file) with the **current offset** and the **flags**. The
`of_refcount` lets several file descriptors share one `openfile` (needed for
`dup2` and for `fork` inheritance).

### 2.2 The per-process fd table

```c
#define FD_MAX 32

struct fdarray {
	struct openfile *fd_table[FD_MAX];
};
```

Each process has an `fdarray` (`proc->p_fdtable`). A **file descriptor** is just
an index into `fd_table[]`. Descriptor `5`, for instance, is
`proc->p_fdtable.fd_table[5]`, which points to an `openfile`.

> **Design choice in this codebase:** fds **0, 1, 2** (STDIN/STDOUT/STDERR) are
> *not* stored in the table. They are special-cased and routed to the console.
> Real files therefore always get fd **≥ 3**. This is why `fdalloc` below starts
> its search at 3.

### 2.3 The fd-table helper functions (in `proc.c`)

These are the primitives every file syscall uses.

**`fdalloc` — install an openfile, return the smallest free fd ≥ 3:**

```c
int
fdalloc(struct proc *proc, struct openfile *of, int *retval)
{
	int fd;
	/* fds 0,1,2 are the console; real files start at 3 so an opened
	 * file never collides with a console fd. */
	for (fd = 3; fd < FD_MAX; fd++) {
		if (proc->p_fdtable.fd_table[fd] == NULL) {
			proc->p_fdtable.fd_table[fd] = of;
			*retval = fd;
			return 0;
		}
	}
	return EMFILE;                 /* table full */
}
```

**`fdget` — look up an fd, hand back the openfile and take a reference:**

```c
int
fdget(struct proc *proc, int fd, struct openfile **ret)
{
	struct openfile *of;

	if (fd < 0 || fd >= FD_MAX) return EBADF;
	of = proc->p_fdtable.fd_table[fd];
	if (of == NULL) return EBADF;

	of->of_refcount++;             /* caller must of_release() later */
	*ret = of;
	return 0;
}
```

**`fddup` / `of_release` / `fdclose` — reference counting:**

```c
void fddup(struct openfile *of) { of->of_refcount++; }

void
of_release(struct openfile *of)
{
	of->of_refcount--;
	if (of->of_refcount == 0) {    /* last reference gone */
		vfs_close(of->of_vnode);   /* drop the vnode */
		kfree(of);                 /* free the struct */
	}
}

void
fdclose(struct proc *proc, int fd)
{
	struct openfile *of = proc->p_fdtable.fd_table[fd];
	if (of == NULL) return;
	proc->p_fdtable.fd_table[fd] = NULL;
	of_release(of);                /* drop the slot's reference */
}
```

The rule to remember: **`fdget` bumps the refcount, so every `fdget` must be
paired with an `of_release`.** This is what keeps an `openfile` alive while a
read/write is in flight even if another thread closes the fd.

---

## 3. `open` — creating a file descriptor

`sys_open` is the archetype; almost every other syscall reuses the same three
moves (copy the path in, do the VFS work, translate errors out).

```c
int
sys_open(const_userptr_t upath, int flags, mode_t mode, int32_t *retval)
{
	char *path;
	struct vnode *vn;
	struct openfile *of;
	int result, fd;

	(void)mode;
	if (flags < 0) return EINVAL;

	/* 1. Copy the pathname from user space into a kernel buffer. */
	result = copyin_string(upath, &path, PATH_MAX);
	if (result) return result;

	/* 2. Ask the VFS to open it; get back a vnode. */
	result = vfs_open(path, flags, mode, &vn);
	if (result) { kfree(path); return result; }

	/* 3. Wrap the vnode in an openfile. */
	of = openfile_alloc(vn, flags);
	if (of == NULL) { vfs_close(vn); kfree(path); return ENOMEM; }

	/* 3a. O_APPEND: start the offset at end-of-file. */
	if (flags & O_APPEND) {
		struct stat st;
		result = VOP_STAT(vn, &st);
		if (result) { kfree(of); vfs_close(vn); kfree(path); return result; }
		of->of_offset = st.st_size;
	}

	/* 4. Install it in the fd table; the index is the fd we return. */
	result = fdalloc(curproc, of, &fd);
	if (result) { kfree(of); vfs_close(vn); kfree(path); return result; }

	kfree(path);
	*retval = fd;                 /* userland sees this integer */
	return 0;
}
```

Two helper details:

- **`copyin_string`** safely copies a NUL-terminated string from user space into
  a freshly `kmalloc`'d kernel buffer using `copyinstr` (which cannot be fooled
  into reading past the user region). It's used by every path-taking syscall.
  ```c
  static int
  copyin_string(const_userptr_t us, char **out, size_t maxlen)
  {
  	char *kbuf = kmalloc(maxlen);
  	size_t got;
  	int result;
  	if (kbuf == NULL) return ENOMEM;
  	result = copyinstr(us, kbuf, maxlen, &got);
  	if (result) { kfree(kbuf); return result; }
  	if (got == 0) { kfree(kbuf); return EINVAL; }
  	*out = kbuf;
  	return 0;
  }
  ```
- **`openfile_alloc`** mallocs an `openfile`, sets `of_offset = 0`,
  `of_refcount = 1` (the one fd slot about to hold it).

> The `kprintf("[KDBG] ...")` lines you'll see in the source are debug traces;
> they don't change behaviour, they just narrate each step on the console.

---

## 4. `read` and `write` — the shared `fd_io` engine

`read` and `write` are *almost the same code*, so they funnel through one static
helper, `fd_io`, with a direction flag (`UIO_READ` vs `UIO_WRITE`).

### 4.1 Console vs file routing

```c
int
sys_read(int fd, userptr_t buf, size_t buflen, int32_t *retval)
{
	if (fd == STDIN_FILENO) {                 /* fd 0 -> console */
		extern int console_read(userptr_t, size_t, int32_t *);
		return console_read(buf, buflen, retval);
	}
	return fd_io(fd, buf, buflen, UIO_READ, retval);
}

int
sys_write(int fd, const_userptr_t buf, size_t nbytes, int32_t *retval)
{
	if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {   /* fd 1/2 -> console */
		extern int console_write(const_userptr_t, size_t, int32_t *);
		return console_write(buf, nbytes, retval);
	}
	return fd_io(fd, (userptr_t)buf, nbytes, UIO_WRITE, retval);
}
```

This is the other half of the "0/1/2 aren't in the table" design: those fds are
detected here and sent to the console driver; everything else is a real file and
goes to `fd_io`.

### 4.2 The `fd_io` helper

```c
static int
fd_io(int fd, userptr_t buf, size_t buflen, enum uio_rw rw, int32_t *retval)
{
	struct proc *p = curproc;
	struct openfile *of;
	struct iovec iov;
	struct uio kuio;
	int result;
	off_t off;

	if (buflen == 0) { *retval = 0; return 0; }

	/* 1. Resolve the fd (also takes a reference on the openfile). */
	result = fdget(p, fd, &of);
	if (result) return result;

	/* 2. Build a uio describing the transfer to/from the USER buffer. */
	if (of->of_vnode == NULL || !VOP_ISSEEKABLE(of->of_vnode)) {
		/* Non-seekable (device-like): no offset. */
		uio_kinit(&iov, &kuio, buf, buflen, 0, rw);
	} else {
		off = of->of_offset;                    /* seekable: use file position */
		uio_kinit(&iov, &kuio, buf, buflen, off, rw);
	}
	kuio.uio_segflg = UIO_USERSPACE;            /* buffer lives in user space   */
	kuio.uio_space  = proc_getas();             /* ...in this process's addrspace */

	/* 3. O_APPEND write: force the offset to current end-of-file. */
	if (rw == UIO_WRITE && (of->of_flags & O_APPEND)) {
		struct stat st;
		result = VOP_STAT(of->of_vnode, &st);
		if (result) { of_release(of); return result; }
		kuio.uio_offset = st.st_size;
	}

	/* 4. Do the actual I/O through the vnode op. */
	result = (rw == UIO_READ) ? VOP_READ(of->of_vnode, &kuio)
	                          : VOP_WRITE(of->of_vnode, &kuio);

	/* 5. Advance the stored file position for seekable files. */
	if (result == 0 && of->of_vnode != NULL && VOP_ISSEEKABLE(of->of_vnode)) {
		of->of_offset = kuio.uio_offset;
	}

	of_release(of);                             /* pair with fdget */

	/* 6. Bytes transferred = requested - leftover (uio_resid). */
	*retval = (int32_t)(buflen - kuio.uio_resid);
	return result;
}
```

The important concepts:

- **`uio`** is OS/161's "I/O descriptor" — it says *where* the data buffer is
  (user vs kernel space, which address space), *how much* (`uio_resid` counts
  bytes remaining), and *at what offset*. `VOP_READ`/`VOP_WRITE` consume it and
  decrement `uio_resid` by however many bytes moved.
- **Bytes actually transferred = `buflen - kuio.uio_resid`** — that's the value
  `read`/`write` return to userland (which is why a short read returns fewer
  bytes than requested, and EOF returns 0).
- **The seek pointer** (`of_offset`) lives in the kernel's `openfile`, so
  successive reads/writes continue where the last one left off. `O_APPEND`
  overrides that by re-stat'ing the file and writing at the true end each time.

---

## 5. `lseek`, `close`, `dup2`

**`lseek`** moves `of_offset` per `SEEK_SET` / `SEEK_CUR` / `SEEK_END`, with
underflow/overflow guards (it refuses to move the position negative). For
`SEEK_END` it uses `VOP_STAT` to find the file size:

```c
case SEEK_END:
	result = VOP_STAT(of->of_vnode, &st);
	if (result) { of_release(of); return result; }
	of->of_offset = st.st_size + pos;
	if (of->of_offset < 0) { of_release(of); return EINVAL; }
	break;
```

Only seekable vnodes are allowed; seeking the console returns `ESPIPE`.

**`close`** validates the fd then delegates to `fdclose`, which nulls the slot
and calls `of_release` (freeing the `openfile` + vnode when the last reference
goes):

```c
int
sys_close(int fd, int32_t *retval)
{
	struct proc *p = curproc;
	(void)retval;
	if (fd < 0 || fd >= FD_MAX)            return EBADF;
	if (p->p_fdtable.fd_table[fd] == NULL) return EBADF;
	fdclose(p, fd);
	return 0;
}
```

**`dup2(oldfd, newfd)`** makes `newfd` refer to the *same* `openfile` as
`oldfd` (shared offset and flags). It closes `newfd` first if it was open, then
points the slot at the same struct and bumps the refcount with `fddup`:

```c
result = fdget(p, oldfd, &src);            /* +1 ref (our hold) */
if (p->p_fdtable.fd_table[newfd] != NULL) fdclose(p, newfd);
fddup(src);                                /* +1 ref for the new slot */
p->p_fdtable.fd_table[newfd] = src;
of_release(src);                           /* drop our hold; slot keeps its ref */
*retval = newfd;
```

---

## 6. Directory & link operations — `link`, `unlink`, `symlink`, `readlink`

These are all "copy the path(s) in, call the matching `vfs_*`, free the buffers".
They carry no fd or offset, so they're short.

**Hard link — `link(old, new)`** makes `new` another name for the same file:

```c
int
sys_link(const_userptr_t uold, const_userptr_t unew, int32_t *retval)
{
	char *oldpath, *newpath;
	int result;
	(void)retval;
	result = copyin_string(uold, &oldpath, PATH_MAX);
	if (result) return result;
	result = copyin_string(unew, &newpath, PATH_MAX);
	if (result) { kfree(oldpath); return result; }
	result = vfs_link(oldpath, newpath);
	kfree(oldpath); kfree(newpath);
	return result;
}
```

**`unlink(path)`** removes a name (this is what userland `remove()` calls):

```c
result = copyin_string(upath, &path, PATH_MAX);
if (result) return result;
result = vfs_remove(path);
kfree(path);
```

**Symbolic link — `symlink(contents, path)`** creates a new symlink at `path`
whose text is `contents`:

```c
result = copyin_string(ucontents, &contents, PATH_MAX);
...
result = copyin_string(upath, &path, PATH_MAX);
...
result = vfs_symlink(contents, path);
```

**`readlink(path, buf, buflen)`** reads a symlink's target text back into a user
buffer, via a `uio` (like read/write) pointed at the user buffer:

```c
uio_kinit(&iov, &kuio, ubuf, ubuflen, 0, UIO_READ);
kuio.uio_segflg = UIO_USERSPACE;
kuio.uio_space  = proc_getas();
result = vfs_readlink(path, &kuio);
...
*retval = (int32_t)(ubuflen - kuio.uio_resid);   /* bytes written */
```

**`mkdir`/`rmdir`** follow the identical copy-in → `vfs_mkdir`/`vfs_rmdir`
pattern.

**`stat`/`fstat`/`lstat`** fill a `struct stat` (size, mode, link count, …) via
`VOP_STAT` and `copyout` it to the user buffer. `fstat` takes an fd (via
`fdget`); `stat`/`lstat` take a path (via `vfs_lookup`). In this SFS-based
assignment, `lstat` is just `stat` because the filesystem doesn't dereference
symlinks at the VFS layer.

---

## 7. The userland test programs

These are ordinary user programs in `src/userland/testbin/`. You run them from
the menu with `p /testbin/<name> [args]` (see §9), or the shell. They call the
libc wrappers that trap into the syscalls above.

### 7.1 `filetest` — the basic round-trip (`file test`)

[filetest.c](../src/userland/testbin/filetest/filetest.c) proves the plumbing
works: create → write 40 bytes → close → reopen → read them back → compare →
remove.

```c
fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0664);   /* sys_open  */
write(fd, writebuf, 40);                            /* sys_write */
close(fd);                                          /* sys_close */

fd = open(file, O_RDONLY);
read(fd, readbuf, 40);                              /* sys_read  */
close(fd);

if (strcmp(readbuf, writebuf)) errx(1, "Buffer data mismatch!");
remove(file);                                       /* sys_unlink */
printf("Passed filetest.\n");
```

If what you read back differs from what you wrote, the offset tracking or the
data path is broken. `remove()` at the end exercises `unlink`.

### 7.2 `file_rwc` — read / write / append / **c**opy

[file_rwc.c](../src/userland/testbin/file_rwc/file_rwc.c) is a richer exercise of
the same syscalls plus `O_APPEND` and `fstat`. Its four steps:

1. **write** `"hello\n"` to `src` with `O_WRONLY|O_CREAT|O_TRUNC`.
2. **append** `"world\n"` by reopening with `O_WRONLY|O_APPEND` — this exercises
   the append offset logic in both `sys_open` (initial offset = EOF) and `fd_io`
   (each write re-seeks to true EOF).
3. **fstat** the file to confirm the size is now 12 bytes.
4. **copy** `src` → `dst` with a userspace read/write loop
   (`copy_file`), then read `dst` back and print it.

It wraps `write`/`read` in retry loops (`write_all`, `read_n`) so partial
transfers and `EINTR` are handled correctly:

```c
static int
write_all(int fd, const char *data, size_t len)
{
	size_t done = 0;
	while (done < len) {
		ssize_t n = write(fd, data + done, len - done);
		if (n < 0) { if (errno == EINTR) continue; return -1; }
		done += (size_t)n;
	}
	return 0;
}
```

The many `[copy_file] step ...` prints are deliberate progress traces so you can
see exactly where it is if a syscall hangs or misbehaves.

### 7.3 `file_link` — symlink + readlink (`file link`)

[file_link.c](../src/userland/testbin/file_link/file_link.c) is tiny and targets
exactly the two link syscalls:

```c
/* /testbin/file_link <contents> <linkname> */
if (symlink(contents, name) < 0) { ... }          /* sys_symlink */

memset(buf, 0, sizeof(buf));
n = readlink(name, buf, sizeof(buf) - 1);          /* sys_readlink */
buf[n] = '\0';
printf("[readlink] %s = %s (%d bytes)\n", name, buf, (int)n);
```

It creates a symlink pointing at `<contents>`, reads it back, and prints both —
so you can visually confirm the target text survived the round-trip and the
byte-count return value is right.

### 7.4 `fileoperations` — a multi-mode file utility

[fileoperations.c](../src/userland/testbin/fileoperations/fileoperations.c) is a
small `cat`/`cp`-style tool that drives the core file syscalls on **real files**
(e.g. `/testbin/sample.txt`) with a mode selected by the first argument:

| Command | Syscalls exercised | What it does |
|---------|--------------------|--------------|
| `cat file [file ...]` (or `read`) | `open`, `read`, `write`(stdout), `close` | print each file to the console |
| `write file text...` | `open` (`O_CREAT|O_TRUNC`), `write`, `close` | create/overwrite a file with `text\n` |
| `append file text...` | `open` (`O_APPEND`), `write`, `close` | append `text\n` to an existing file |
| `copy src dst` | `open`×2, `read`, `write`, `close`×2 | byte-for-byte copy `src` → `dst` |

The `main` is just a dispatcher on `argv[1]`; anything unrecognized falls back to
`cat`ing every argument (so `p /testbin/fileoperations /some/file` still works):

```c
int
main(int argc, char **argv)
{
	const char *mode = argv[1];

	if (!strcmp(mode, "cat") || !strcmp(mode, "read"))
		return do_cat(argc - 2, argv + 2);
	if (!strcmp(mode, "write"))
		return do_write(argc - 2, argv + 2, 0 /* truncate */);
	if (!strcmp(mode, "append"))
		return do_write(argc - 2, argv + 2, 1 /* append */);
	if (!strcmp(mode, "copy"))
		return do_copy(argc - 2, argv + 2);

	/* Unknown mode: treat every argument as a file to cat. */
	return do_cat(argc - 1, argv + 1);
}
```

**`cat`** loops `read` → `write(STDOUT_FILENO, ...)` a `BUFSZ`-byte buffer at a
time until EOF (`read` returns 0), which is exactly the read-side of the syscall
path in §4:

```c
static int
cat_fd(int fd, const char *name)
{
	char buf[BUFSZ];
	ssize_t r;
	for (;;) {
		r = read(fd, buf, sizeof(buf));
		if (r == 0) break;                       /* EOF */
		if (r < 0) { if (errno == EINTR) continue; return -1; }
		if (write_all(STDOUT_FILENO, buf, (size_t)r) < 0) return -1;
	}
	return 0;
}
```

**`write` / `append`** share `do_write`; the only difference is the open flags —
`O_WRONLY|O_CREAT|O_TRUNC` to overwrite versus `O_WRONLY|O_APPEND` to append,
which is what exercises the append-offset logic (`of_offset = st.st_size`) in
`sys_open` and `fd_io`:

```c
if (append) flags = O_WRONLY | O_APPEND;              /* file must exist */
else        flags = O_WRONLY | O_CREAT | O_TRUNC;
fd = open(path, flags, 0644);
write_all(fd, text, len);
close(fd);
```

(The text is rebuilt from the menu-tokenized words by `join_words`, which joins
`argv` with single spaces and adds a trailing newline — needed because the menu
splits your line on spaces.)

**`copy`** is the read/write loop again, but writing into a second fd instead of
stdout — a userspace `cp`:

```c
sfd = open(src, O_RDONLY);
dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
for (;;) {
	r = read(sfd, buf, sizeof(buf));
	if (r == 0) break;
	if (r < 0) { if (errno == EINTR) continue; ... }
	write_all(dfd, buf, (size_t)r);
	total += (size_t)r;
}
```

All modes wrap `write` in `write_all` (retry on short/`EINTR` writes) so partial
transfers are handled correctly. Typical usage from the menu (argument passing
must be enabled):

```
p /testbin/fileoperations write  /note.txt hello there
p /testbin/fileoperations append /note.txt second line
p /testbin/fileoperations cat    /note.txt
p /testbin/fileoperations copy   /note.txt /note.bak
```

---

## 8. The `pf` menu command — `printfile` (kernel-side file dump)

`pf` is different from the userland tests: it runs **inside the kernel**, so it
doesn't use syscalls or fds at all — it talks to the VFS directly with `vfs_open`
and `VOP_READ`/`VOP_WRITE`. It's wired in the menu command table as
`{ "pf", printfile }`, and `printfile` lives in
[fstest.c](../src/kern/test/fstest.c).

```c
int
printfile(int nargs, char **args)
{
	struct vnode *rv, *wv;                 /* read vnode, write vnode */
	struct iovec iov;
	struct uio ku;
	off_t rpos = 0, wpos = 0;
	char buf[128];
	char outfile[16];
	int result, done = 0;

	if (nargs != 2) { kprintf("Usage: pf filename\n"); return EINVAL; }

	strcpy(outfile, "con:");               /* the console device */

	result = vfs_open(args[1], O_RDONLY, 0664, &rv);   /* open the input file  */
	if (result) { kprintf("printfile: %s\n", strerror(result)); return result; }

	result = vfs_open(outfile, O_WRONLY, 0664, &wv);   /* open the console out */
	if (result) { vfs_close(rv); return result; }

	while (!done) {
		/* read a chunk from the file at rpos */
		uio_kinit(&iov, &ku, buf, sizeof(buf), rpos, UIO_READ);
		result = VOP_READ(rv, &ku);
		if (result) break;
		rpos = ku.uio_offset;
		if (ku.uio_resid > 0) done = 1;    /* short read => EOF */

		/* write what we got to the console at wpos */
		uio_kinit(&iov, &ku, buf, sizeof(buf) - ku.uio_resid, wpos, UIO_WRITE);
		result = VOP_WRITE(wv, &ku);
		if (result) break;
		wpos = ku.uio_offset;
	}

	vfs_close(wv);
	vfs_close(rv);
	return 0;
}
```

So `pf myfile` copies `myfile` to `con:` (the console) 128 bytes at a time using
kernel-space uios. It's the kernel counterpart to `cat`, and a handy way to prove
the VFS read path works before your userland syscalls are done.

---

## 9. Executing a program from the menu — `p` and `s`

The Operations menu (`?o`) has:

```
[s]  Shell
[p]  Other program
```

Both end up in the same place. Here's the full path.

### 9.1 `cmd_prog` (the `p` command) and `cmd_shell` (`s`)

```c
static int
cmd_prog(int nargs, char **args)
{
	if (nargs < 2) { kprintf("Usage: p program [arguments]\n"); return EINVAL; }
	args++;                       /* drop the leading "p" so args[0] = program */
	nargs--;
	return common_prog(nargs, args);
}

static int
cmd_shell(int nargs, char **args)
{
	(void)args;
	if (nargs != 1) { kprintf("Usage: s\n"); return EINVAL; }
	args[0] = (char *)_PATH_SHELL;    /* "/bin/sh" */
	return common_prog(nargs, args);
}
```

`p /testbin/filetest foo` becomes `common_prog(2, {"/testbin/filetest","foo"})`.
`s` just hard-codes the program to `/bin/sh` and calls the same helper.

### 9.2 `common_prog` — fork a process, wait for it

```c
static int
common_prog(int nargs, char **args)
{
	struct proc *proc;
	int result;

	/* 1. Create a fresh process to host the program. */
	proc = proc_create_runprogram(args[0] /* name */);
	if (proc == NULL) return ENOMEM;

	/* 2. Fork a thread in that process that will run the program. */
	result = thread_fork(args[0],           /* thread name              */
	                     proc,              /* the new process          */
	                     cmd_progthread,    /* thread entry function    */
	                     args, nargs);      /* args passed through      */
	if (result) {
		kprintf("thread_fork failed: %s\n", strerror(result));
		proc_destroy(proc);
		return result;
	}

	/* 3. Wait for the program to finish before returning to the menu. */
	result = proc_wait(proc->p_pid, NULL, 0, NULL);
	if (result) { kprintf("waitpid failed: %s\n", strerror(result)); return result; }

	return 0;
}
```

- **`proc_create_runprogram`** builds a new `struct proc` (its own address space
  will be created inside `runprogram`, its own fd table, a fresh pid).
- **`thread_fork`** starts a kernel thread *inside that new process* whose entry
  point is `cmd_progthread`.
- **`proc_wait`** makes the menu block until the child exits, so the kernel menu
  and the user program don't both try to read the console at once.

### 9.3 `cmd_progthread` — the new thread's entry

```c
static void
cmd_progthread(void *ptr, unsigned long nargs)
{
	char **args = ptr;
	char progname[128];
	int result;

	KASSERT(nargs >= 1);
	KASSERT(strlen(args[0]) < sizeof(progname));

	/* runprogram()'s vfs_open() may clobber the pathname buffer, so give
	 * it a private copy and keep the original argv intact for copyout. */
	strcpy(progname, args[0]);

	result = runprogram(progname, (int)nargs, args);
	if (result) {
		kprintf("Running program %s failed: %s\n", args[0], strerror(result));
		return;
	}
	/* NOTREACHED: runprogram only returns on error. */
}
```

It copies the program name (because `vfs_open` can mangle its buffer) and calls
`runprogram`.

### 9.4 `runprogram` — ELF file → running user process

This is the heart of program execution ([runprogram.c](../src/kern/syscall/runprogram.c)):

```c
int
runprogram(char *progname, int argc, char **argv)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	userptr_t argv_userptr = NULL;
	int result;

	/* 1. Open the executable file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) return result;

	KASSERT(proc_getas() == NULL);      /* we must be a brand-new process */

	/* 2. Create and switch to a fresh address space. */
	as = as_create();
	if (as == NULL) { vfs_close(v); return ENOMEM; }
	proc_setas(as);
	as_activate();

	/* 3. Load the ELF image (text/data segments) into the address space. */
	result = load_elf(v, &entrypoint);
	if (result) { vfs_close(v); return result; }
	vfs_close(v);                        /* done with the file */

	/* 4. Set up the user stack. */
	result = as_define_stack(as, &stackptr);
	if (result) return result;

	/* 5. Copy argv onto the new user stack (so the program sees its args). */
	if (argc > 0 && argv != NULL) {
		result = copyout_args(argv, argc, &stackptr, &argv_userptr);
		if (result) return result;
	}

	/* 6. Jump to user mode at the ELF entry point. Never returns. */
	enter_new_process(argc, argv_userptr, NULL /*environ*/, stackptr, entrypoint);

	panic("enter_new_process returned\n");
	return EINVAL;
}
```

The six stages:

1. **`vfs_open`** the program file and get a vnode.
2. **`as_create` + `proc_setas` + `as_activate`** — give the process a fresh,
   empty virtual address space and switch the MMU to it.
3. **`load_elf`** parses the ELF headers and loads the code/data segments into
   that address space, returning the `entrypoint` (where `main`'s startup code
   begins).
4. **`as_define_stack`** reserves the user stack region and returns the initial
   `stackptr`.
5. **`copyout_args`** marshals the kernel-side `argv` strings onto the user stack
   in the layout the C runtime expects, and returns the user-space address of the
   `argv` array. (This is how `p /testbin/filetest foo` delivers `"foo"` to the
   program's `main(argc, argv)`.)
6. **`enter_new_process`** performs the actual privilege drop: it switches the
   CPU to user mode and jumps to `entrypoint`, with `argc`/`argv`/`stackptr` set
   up. It **does not return** — from here on the user program is running. When
   that program later calls `_exit`, control returns to the waiting
   `proc_wait` in `common_prog`, which then returns to the menu loop.

### 9.5 The whole `p` chain at a glance

```
you type:  p /testbin/filetest foo
   menu()  -> menu_execute() -> cmd_dispatch()   (matches "p" in cmdtable)
      cmd_prog()            drops the "p", leaving argv = {"/testbin/filetest","foo"}
         common_prog()
            proc_create_runprogram()   new process
            thread_fork(cmd_progthread) new thread in that process
               cmd_progthread()
                  runprogram()
                     vfs_open + as_create + load_elf + as_define_stack
                     copyout_args
                     enter_new_process()  --> USER MODE: filetest's main() runs
            proc_wait()      menu blocks until filetest exits
      back to the prompt
```

---

## 10. One-paragraph summary

File operations are a three-layer stack: userland programs (`filetest`,
`file_rwc`, `file_link`, `fileoperations`) call libc wrappers that trap into the kernel
syscalls in [file_syscalls.c](../src/kern/syscall/file_syscalls.c), which validate
arguments, copy strings/data across the user–kernel boundary, track each open
file as a reference-counted `struct openfile` in a per-process fd table
(fds 0/1/2 reserved for the console, real files ≥ 3), and forward the real work
to the VFS via `vfs_*`/`VOP_*`. `read` and `write` share the `fd_io` engine,
which uses a `uio` to move bytes and updates the stored `of_offset`; `link`,
`unlink`, `symlink`, `readlink`, `mkdir`, `stat`, etc. are thin copy-in-then-call
wrappers around the matching `vfs_*` routine. The kernel-side `pf` command
(`printfile`) bypasses syscalls and dumps a file to the console directly through
the VFS. To run a program from the menu you use `p prog [args]` (or `s` for the
shell): `cmd_prog` → `common_prog` creates a process and forks a thread into
`cmd_progthread` → `runprogram`, which opens the ELF, builds an address space,
`load_elf`s the image, sets up the stack, copies `argv` out, and
`enter_new_process` jumps to user mode; the menu meanwhile `proc_wait`s for the
child to exit before showing the prompt again.
