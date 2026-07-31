# Assignment 2 Implementation - File System Syscalls

This document describes the implementation added for OS/161 Assignment 2.
The assignment asks for several missing pieces:

1. `sfs_readlink` and `sfs_symlink` in the SFS file system.
2. Name validation in `sfs_namefile` (used by `__getcwd`).
3. File system syscalls: `open`, `read`, `write`, `lseek`, `close`,
   `dup2`, `chdir`, `__getcwd`, `mkdir`, `rmdir`, `link`, `unlink`,
   `symlink`, `readlink`, `fstat`, `stat`, `lstat`.
4. Userland wrappers in libc so user programs can use the new
   syscalls through familiar C functions.

In addition, the assignment says to "implement file operations like
read, write, copy, append". The kernel provides `read`, `write`, and
the `O_APPEND` open flag; the test program `testbin/file_rwc`
demonstrates user-level `read`, `write`, `copy` (a read/write loop),
and `append` (open with `O_APPEND`).

## Files Added or Modified

### SFS layer

- `src/kern/include/kern/sfs.h`
  - Added `SFS_TYPE_LINK = 3` to on-disk inode type enum.
- `src/kern/include/sfs.h`
  - Forward declaration of `sfs_linkops` (covered by `sfsprivate.h`).
- `src/kern/fs/sfs/sfsprivate.h`
  - Declares `sfs_linkops`.
- `src/kern/fs/sfs/sfs_inode.c`
  - Recognises `SFS_TYPE_LINK` and selects `sfs_linkops` for symlink
    vnodes in `sfs_loadvnode`.
- `src/kern/fs/sfs/sfs_vnops.c`
  - Added `sfs_readlink` (returns `EINVAL` on non-link inodes, else
    delegates to `sfs_io`).
  - Added `sfs_symlink` (allocates a new `SFS_TYPE_LINK` inode, links
    it into the directory, then writes the contents with `sfs_io`).
  - Updated `sfs_stat` to also fill in `st_ino`.
  - Updated `sfs_gettype` to return `S_IFLNK` for symlinks.
  - Updated `sfs_namefile` to validate that the uio has at least one
    byte of room, so `__getcwd` cannot spin or over-read.
  - Wired `sfs_symlink` into `sfs_dirops`.
  - Wired `sfs_readlink` into `sfs_linkops`.
  - Added the new `sfs_linkops` vnode operations table.

### Process / file descriptor layer

- `src/kern/include/proc.h`
  - Added `struct openfile`, `struct fdarray`, and `FD_MAX = 32`.
  - Added `p_fdtable` to `struct proc`.
  - Declared `fdinit`, `fdalloc`, `fdget`, `fddup`, `of_release`,
    `fdclose`.
- `src/kern/proc/proc.c`
  - Added `fdinit`/`fdalloc`/`fdget`/`fddup`/`of_release`/`fdclose`
    plus an internal `fd_destroy_all`.
  - Calls `fdinit` when creating each proc.
  - Calls `fd_destroy_all` from `proc_exit`.

### File-system syscalls

- `src/kern/syscall/file_syscalls.c` *(new)*
  - Implements all the file system syscalls listed in the assignment.
  - Uses a per-process fd table (`p_fdtable`) keyed by file descriptor
    number.
  - Console `read`/`write` on stdin/stdout/stderr is preserved by
    routing those fds through `console_read`/`console_write` defined
    in `console_syscalls.c`.
  - Open files track their current offset so `lseek`/`read`/`write`
    interact correctly.
- `src/kern/syscall/console_syscalls.c`
  - Refactored to expose `console_read`/`console_write` as helpers.
  - The exported `sys_read`/`sys_write` only operate on the special
    fds 0/1/2; everything else goes through the fd table now.
- `src/kern/include/syscall.h`
  - Added prototypes for the new syscalls and the two console
    helpers.
- `src/kern/arch/mips/syscall/syscall.c`
  - Added `case` arms for the new syscall numbers.

### Build wiring

- `src/kern/conf/conf.kern`
  - Added `syscall/file_syscalls.c`.
- `src/kern/compile/DUMBVM/files.mk`
  - Added `syscall/file_syscalls.c`.

### Userland wrappers

- `src/userland/lib/libc/unix/fileops.c` *(new)*
  - Userland wrappers: `open`, `close`, `read`, `write`, `lseek`,
    `dup2`, `chdir`, `__getcwd`, `mkdir`, `rmdir`, `link`,
    `remove`, `symlink`, `readlink`, `fstat`, `stat`, `lstat`.
- `src/userland/lib/libc/Makefile`
  - Added `unix/fileops.c` to the build.

### Test programs

- `src/userland/testbin/file_rwc/` *(new)*
  - Creates a source file via `open(O_WRONLY|O_CREAT|O_TRUNC)` and
    `write`.
  - Reopens with `O_APPEND` and appends more data.
  - Copies the source file to a destination using `read`/`write`.
  - Reads the destination back and prints it.
- `src/userland/testbin/file_link/` *(new)*
  - Creates a symlink with `symlink(target, name)`.
  - Reads it back with `readlink`.

## How the File Operations Work

| Operation | Kernel entry point | Path through the kernel |
|-----------|--------------------|--------------------------|
| `read` | `sys_read` | If fd is `STDIN_FILENO`, use `console_read`. Otherwise `fd_io` -> `VOP_READ` -> `sfs_io`. |
| `write` | `sys_write` | If fd is `STDOUT_FILENO`/`STDERR_FILENO`, use `console_write`. Otherwise `fd_io` -> `VOP_WRITE` -> `sfs_io`. |
| `append` | `sys_open` with `O_APPEND` | After `vfs_open`, look up current size with `VOP_STAT`, set `of_offset = st_size` so subsequent writes always start at EOF. |
| `copy` | user-level `read`+`write` loop | Both routed through the kernel syscalls. |

The `read`/`write` loop in `copy_file()` in `file_rwc.c` is the
"copy" required by the assignment. The `O_APPEND` open plus an
explicit `write` after reopening is the "append" path.

## Open File / FD Table Model

```c
struct openfile {
    struct vnode *of_vnode;   // the underlying file
    off_t of_offset;          // current file pointer for sequential ops
    int of_flags;             // O_RDONLY/O_WRONLY/O_RDWR/O_APPEND
    int of_refcount;          // 1 per fd slot pointing here
};

struct fdarray {
    struct openfile *fd_table[FD_MAX];  // FD_MAX = 32
};
```

`struct proc` holds an `fdtable`. The helpers keep reference counts
correct:

* `fdalloc` installs a new `openfile` into the lowest free slot.
* `fdget` returns the openfile and bumps its refcount.
* `of_release` decrements the refcount and frees the openfile (and
  decrefs the vnode) if it reaches zero.
* `fdclose` clears the slot and calls `of_release`.

`dup2` shares a single openfile between two fd slots by bumping the
refcount, and `proc_exit` calls `fd_destroy_all` so terminated
processes don't leak vnodes.

## Limitations

* `lseek` returns the new offset through `int32_t *retval`. The
  syscall dispatcher only writes the 32-bit `v0` register. Offsets
  beyond `2^31` cannot round-trip through this interface. SFS
  volumes are small enough that this is fine in practice.
* `lstat` calls the same code path as `stat` because SFS does not
  dereference symlinks during `vfs_lookup`. Adding symlink
  dereferencing to `vfs_lookup` would let `lstat` differ from `stat`.
* `fchdir` is not implemented (assignment did not require it).
* The file descriptor table is per-process and is not inherited
  across `fork` (assignment did not require fork+fd inheritance
  either).
* Concurrency is gated by the global `vfs_biglock` because the
  lock/CV primitives are partial.