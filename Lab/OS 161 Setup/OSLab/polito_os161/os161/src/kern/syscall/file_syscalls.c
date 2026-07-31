/*
 * Copyright (c) 2026
 *	Assignment 2 implementation file.
 *
 * Redistribution and use in source and binary forms are permitted for
 * academic use.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <kern/unistd.h>
#include <lib.h>
#include <current.h>
#include <proc.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <copyinout.h>
#include <syscall.h>

/*
 * File system syscalls for Assignment 2.
 *
 * Implemented:
 *   open, close, read, write, lseek
 *   dup2
 *   chdir, __getcwd
 *   mkdir, rmdir
 *   link, unlink (remove)
 *   symlink, readlink
 *   fstat, stat, lstat
 *
 * Concurrency is over-serialized with vfs_biglock (as in the rest of
 * the kernel) since the locking primitives are partial.
 */

/*
 * Helper: allocate an openfile struct for an opened vnode.
 */
static
struct openfile *
openfile_alloc(struct vnode *vn, int flags)
{
	struct openfile *of;

	of = kmalloc(sizeof(*of));
	if (of == NULL) {
		return NULL;
	}
	of->of_vnode = vn;
	of->of_offset = 0;
	of->of_flags = flags;
	of->of_refcount = 1;	/* one fd slot for now */
	return of;
}

/*
 * Helper: copy a NUL-terminated string from userspace into a freshly
 * allocated kernel buffer. The returned buffer must be freed with kfree.
 *
 * maxlen can be PATH_MAX or NAME_MAX depending on context.
 */
static
int
copyin_string(const_userptr_t us, char **out, size_t maxlen)
{
	char *kbuf;
	int result;
	size_t got;

	kbuf = kmalloc(maxlen);
	if (kbuf == NULL) {
		return ENOMEM;
	}

	result = copyinstr(us, kbuf, maxlen, &got);
	if (result) {
		kfree(kbuf);
		return result;
	}
	if (got == 0) {
		kfree(kbuf);
		return EINVAL;
	}

	*out = kbuf;
	return 0;
}

/*
 * sys_open(path, flags, mode)
 */
int
sys_open(const_userptr_t upath, int flags, mode_t mode, int32_t *retval)
{
	char *path;
	struct vnode *vn;
	struct openfile *of;
	int result, fd;

	(void)mode;

	if (flags < 0) {
		return EINVAL;
	}

	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		return result;
	}

	kprintf("[KDBG] sys_open: path=%s flags=0x%x\n", path, flags);

	result = vfs_open(path, flags, mode, &vn);
	if (result) {
		kprintf("[KDBG] sys_open: vfs_open failed for %s flags=0x%x err=%d\n",
		       path, flags, result);
		kfree(path);
		return result;
	}

	of = openfile_alloc(vn, flags);
	if (of == NULL) {
		vfs_close(vn);
		kfree(path);
		return ENOMEM;
	}

	if (flags & O_APPEND) {
		/*
		 * Place the file position past the current end of file
		 * so the next write appends. (O_APPEND semantics.)
		 */
		struct stat st;
		result = VOP_STAT(vn, &st);
		if (result) {
			kfree(of);
			vfs_close(vn);
			kfree(path);
			return result;
		}
		of->of_offset = st.st_size;
	}

	result = fdalloc(curproc, of, &fd);
	if (result) {
		kfree(of);
		vfs_close(vn);
		kfree(path);
		return result;
	}

	kfree(path);
	kprintf("[KDBG] sys_open: OK fd=%d\n", fd);
	*retval = fd;
	return 0;
}

/*
 * sys_close(fd)
 *
 * With our fd model fdclose alone clears the slot and decrements the
 * reference count, freeing the openfile if it goes to zero. We just
 * need to fail cleanly on bad fd numbers; fdclose returns void.
 */
int
sys_close(int fd, int32_t *retval)
{
	struct proc *p = curproc;

	(void)retval;

	if (fd < 0 || fd >= FD_MAX) {
		return EBADF;
	}
	if (p->p_fdtable.fd_table[fd] == NULL) {
		return EBADF;
	}
	fdclose(p, fd);
	return 0;
}

/*
 * Internal helper for read() / write(): resolve an fd, pick up a uio
 * pointing at the user buffer, and call into VOP_READ/VOP_WRITE with
 * the proper offset.
 *
 * fdget bumps the openfile's refcount; we must release it with
 * of_release() regardless of how we exit.
 */
static
int
fd_io(int fd, userptr_t buf, size_t buflen, enum uio_rw rw, int32_t *retval)
{
	struct proc *p = curproc;
	struct openfile *of;
	struct iovec iov;
	struct uio kuio;
	int result;
	off_t off;

	if (buflen == 0) {
		*retval = 0;
		return 0;
	}

	kprintf("[KDBG] fd_io: fd=%d rw=%d buflen=%zu\n", fd, rw, buflen);

	result = fdget(p, fd, &of);
	if (result) {
		kprintf("[KDBG] fd_io: fdget err=%d\n", result);
		return result;
	}

	/* Only seekable regular files honour positional reads/writes here.
	 * The device case (console) is handled by sys_read/sys_write. */
	if (of->of_vnode == NULL || !VOP_ISSEEKABLE(of->of_vnode)) {
		/*
		 * Non-seekable: behave like a sequential, position-less device.
		 */
		uio_kinit(&iov, &kuio, buf, buflen, 0, rw);
	} else {
		off = of->of_offset;
		uio_kinit(&iov, &kuio, buf, buflen, off, rw);
	}
	/* The buffer is in user space, so uiomove will copyin/copyout. */
	kuio.uio_segflg = UIO_USERSPACE;
	kuio.uio_space = proc_getas();

	if (rw == UIO_WRITE && (of->of_flags & O_APPEND)) {
		struct stat st;
		result = VOP_STAT(of->of_vnode, &st);
		if (result) {
			of_release(of);
			return result;
		}
		kuio.uio_offset = st.st_size;
	}

	kprintf("[KDBG] fd_io: calling VOP_%s fd=%d resid=%zu\n",
	       rw == UIO_READ ? "READ" : "WRITE",
	       fd, (size_t)kuio.uio_resid);
	result = (rw == UIO_READ) ? VOP_READ(of->of_vnode, &kuio)
				  : VOP_WRITE(of->of_vnode, &kuio);
	kprintf("[KDBG] fd_io: VOP_%s returned fd=%d result=%d resid=%zu\n",
	       rw == UIO_READ ? "READ" : "WRITE",
	       fd, result, (size_t)kuio.uio_resid);

	if (result == 0 && of->of_vnode != NULL && VOP_ISSEEKABLE(of->of_vnode)) {
		of->of_offset = kuio.uio_offset;
	}

	of_release(of);

	*retval = (int32_t)(buflen - kuio.uio_resid);
	return result;
}

/*
 * sys_read(fd, buf, buflen)
 */
int
sys_read(int fd, userptr_t buf, size_t buflen, int32_t *retval)
{
	if (fd == STDIN_FILENO) {
		/*
		 * Consoles are not tracked in the FD table, so fd_io()
		 * would fail. Route stdin reads through the same kernel
		 * console helper used for the file-table-less build.
		 */
		extern int console_read(userptr_t buf, size_t buflen, int32_t *retval);
		return console_read(buf, buflen, retval);
	}
	return fd_io(fd, buf, buflen, UIO_READ, retval);
}

/*
 * sys_write(fd, buf, nbytes)
 */
int
sys_write(int fd, const_userptr_t buf, size_t nbytes, int32_t *retval)
{
	if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
		extern int console_write(const_userptr_t buf, size_t nbytes,
					 int32_t *retval);
		return console_write(buf, nbytes, retval);
	}
	return fd_io(fd, (userptr_t)buf, nbytes, UIO_WRITE, retval);
}

/*
 * sys_lseek(fd, pos, whence)
 *
 * The kernel returns the new file position through *retval which is a
 * 32-bit word. off_t in OS/161 is 64-bit, but for assignment-scale
 * SFS volumes the lower 32 bits are sufficient and the dispatcher can
 * only return v0 (32-bit) anyway.
 */
int
sys_lseek(int fd, off_t pos, int whence, int32_t *retval)
{
	struct proc *p = curproc;
	struct openfile *of;
	struct stat st;
	int result;

	result = fdget(p, fd, &of);
	if (result) {
		return result;
	}
	if (of->of_vnode == NULL || !VOP_ISSEEKABLE(of->of_vnode)) {
		of_release(of);
		return ESPIPE;
	}

	switch (whence) {
	    case SEEK_SET:
		if (pos < 0) {
			of_release(of);
			return EINVAL;
		}
		of->of_offset = pos;
		break;
	    case SEEK_CUR:
		/* Avoid underflow when seeking before the start. */
		if (pos < 0 && -pos > of->of_offset) {
			of_release(of);
			return EINVAL;
		}
		of->of_offset += pos;
		break;
	    case SEEK_END:
		result = VOP_STAT(of->of_vnode, &st);
		if (result) {
			of_release(of);
			return result;
		}
		of->of_offset = st.st_size + pos;
		if (of->of_offset < 0) {
			of_release(of);
			return EINVAL;
		}
		break;
	    default:
		of_release(of);
		return EINVAL;
	}

	of_release(of);
	*retval = (int32_t)of->of_offset;
	return 0;
}

/*
 * sys_dup2(oldfd, newfd)
 */
int
sys_dup2(int oldfd, int newfd, int32_t *retval)
{
	struct proc *p = curproc;
	struct openfile *src;
	int result;

	if (oldfd < 0 || oldfd >= FD_MAX || newfd < 0 || newfd >= FD_MAX) {
		return EBADF;
	}
	if (oldfd == newfd) {
		*retval = newfd;
		return 0;
	}

	result = fdget(p, oldfd, &src);
	if (result) {
		return result;
	}

	/* If newfd is already open, close it first. */
	if (p->p_fdtable.fd_table[newfd] != NULL) {
		fdclose(p, newfd);
	}

	/* src is now held with refcount incremented by fdget. Make both
	 * slots point at it and bump the refcount one more time for the
	 * extra fd slot. */
	fddup(src);
	p->p_fdtable.fd_table[newfd] = src;

	/* Release our get-reference. The fd slot at oldfd still holds it. */
	of_release(src);

	*retval = newfd;
	return 0;
}

/*
 * sys_chdir(path)
 */
int
sys_chdir(const_userptr_t upath, int32_t *retval)
{
	char *path;
	int result;

	(void)retval;

	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		return result;
	}
	result = vfs_chdir(path);
	kfree(path);
	return result;
}

/*
 * sys___getcwd(buf, buflen)
 */
int
sys___getcwd(userptr_t ubuf, size_t ubuflen, int32_t *retval)
{
	struct iovec iov;
	struct uio kuio;
	int result;

	if (ubuflen == 0 || ubuflen > PATH_MAX) {
		return EINVAL;
	}

	uio_kinit(&iov, &kuio, ubuf, ubuflen, 0, UIO_READ);
	kuio.uio_segflg = UIO_USERSPACE;
	kuio.uio_space = proc_getas();
	result = vfs_getcwd(&kuio);
	if (result) {
		return result;
	}

	/*
	 * Append a NUL terminator at the position where reading stopped.
	 * The user buffer must have at least one extra byte to be safe,
	 * but we also tolerate the buffer being exactly the length returned
	 * because no extra room is required when vfs_getcwd wrote zero
	 * bytes (the cwd is "/").
	 */
	if (ubuflen > (size_t)(ubuflen - kuio.uio_resid)) {
		char nul = '\0';
		result = copyout(&nul, (userptr_t)((vaddr_t)ubuf +
			   (ubuflen - kuio.uio_resid)), sizeof(nul));
		if (result) {
			return result;
		}
	}

	*retval = (int32_t)(ubuflen - kuio.uio_resid + 1);
	return 0;
}

/*
 * sys_mkdir(path, mode)
 */
int
sys_mkdir(const_userptr_t upath, mode_t mode, int32_t *retval)
{
	char *path;
	int result;

	(void)retval;

	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		return result;
	}
	result = vfs_mkdir(path, mode);
	kfree(path);
	return result;
}

/*
 * sys_rmdir(path)
 */
int
sys_rmdir(const_userptr_t upath, int32_t *retval)
{
	char *path;
	int result;

	(void)retval;

	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		return result;
	}
	result = vfs_rmdir(path);
	kfree(path);
	return result;
}

/*
 * sys_link(oldpath, newpath)
 */
int
sys_link(const_userptr_t uold, const_userptr_t unew, int32_t *retval)
{
	char *oldpath, *newpath;
	int result;

	(void)retval;

	result = copyin_string(uold, &oldpath, PATH_MAX);
	if (result) {
		return result;
	}
	result = copyin_string(unew, &newpath, PATH_MAX);
	if (result) {
		kfree(oldpath);
		return result;
	}

	result = vfs_link(oldpath, newpath);
	kfree(oldpath);
	kfree(newpath);
	return result;
}

/*
 * sys_unlink(path)
 */
int
sys_unlink(const_userptr_t upath, int32_t *retval)
{
	char *path;
	int result;

	(void)retval;

	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		return result;
	}
	result = vfs_remove(path);
	kfree(path);
	return result;
}

/*
 * sys_symlink(contents, path)
 */
int
sys_symlink(const_userptr_t ucontents, const_userptr_t upath, int32_t *retval)
{
	char *contents = NULL;
	char *path;
	int result;

	(void)retval;

	result = copyin_string(ucontents, &contents, PATH_MAX);
	if (result) {
		return result;
	}
	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		kfree(contents);
		return result;
	}
	result = vfs_symlink(contents, path);
	kfree(contents);
	kfree(path);
	return result;
}

/*
 * sys_readlink(path, buf, buflen)
 */
int
sys_readlink(const_userptr_t upath, userptr_t ubuf, size_t ubuflen,
	     int32_t *retval)
{
	char *path;
	struct iovec iov;
	struct uio kuio;
	int result;

	if (ubuflen == 0 || ubuflen > PATH_MAX) {
		return EINVAL;
	}

	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		return result;
	}

	uio_kinit(&iov, &kuio, ubuf, ubuflen, 0, UIO_READ);
	kuio.uio_segflg = UIO_USERSPACE;
	kuio.uio_space = proc_getas();
	result = vfs_readlink(path, &kuio);
	kfree(path);
	if (result) {
		return result;
	}

	*retval = (int32_t)(ubuflen - kuio.uio_resid);
	return 0;
}

/*
 * Helper for stat-family syscalls: copyout the stat structure.
 */
static
int
copyout_stat(struct stat *kst, userptr_t ust)
{
	return copyout(kst, ust, sizeof(struct stat));
}

/*
 * sys_fstat(fd, buf)
 */
int
sys_fstat(int fd, userptr_t ustat, int32_t *retval)
{
	struct proc *p = curproc;
	struct openfile *of;
	struct stat kst;
	int result;

	(void)retval;

	result = fdget(p, fd, &of);
	if (result) {
		return result;
	}
	result = VOP_STAT(of->of_vnode, &kst);
	of_release(of);
	if (result) {
		return result;
	}
	return copyout_stat(&kst, ustat);
}

/*
 * sys_stat(path, buf)
 */
int
sys_stat(const_userptr_t upath, userptr_t ustat, int32_t *retval)
{
	char *path;
	struct vnode *vn;
	struct stat kst;
	int result;

	(void)retval;

	result = copyin_string(upath, &path, PATH_MAX);
	if (result) {
		return result;
	}
	result = vfs_lookup(path, &vn);
	if (result) {
		kfree(path);
		return result;
	}
	result = VOP_STAT(vn, &kst);
	VOP_DECREF(vn);
	kfree(path);
	if (result) {
		return result;
	}
	return copyout_stat(&kst, ustat);
}

/*
 * sys_lstat(path, buf)
 *
 * Without symlink dereferencing at the VFS layer (which the assignment
 * SFS does not support), this behaves the same as stat().
 */
int
sys_lstat(const_userptr_t upath, userptr_t ustat, int32_t *retval)
{
	(void)retval;
	return sys_stat(upath, ustat, retval);
}

/*
 * sys_getdirentry(fd, buf, buflen)
 *
 * Read a single directory entry. fd is a file descriptor for an open
 * directory. The kernel reads the next slot (tracked in of->of_offset),
 * writes a NUL-terminated name into buf (up to buflen bytes), and
 * advances the fd's offset to the next slot.
 *
 * This matches the OS/161 userland signature: getdirentry(fd, buf, n).
 * The position is held in the kernel's openfile, not in userspace.
 *
 * Returns 0 on success, ENOTDIR if fd is not a directory, EBADF if fd
 * is invalid, or another error from the underlying vnode.
 */
int
sys_getdirentry(int fd, userptr_t ubuf, size_t buflen, int32_t *retval)
{
	struct proc *p = curproc;
	struct openfile *of;
	struct iovec iov;
	struct uio kuio;
	int result;

	(void)retval;

	result = fdget(p, fd, &of);
	if (result) {
		return result;
	}

	/* Initialize a kernel uio starting at the fd's current slot. */
	uio_kinit(&iov, &kuio, ubuf, buflen, of->of_offset, UIO_READ);
	kuio.uio_segflg = UIO_USERSPACE;
	kuio.uio_space = proc_getas();

	result = VOP_GETDIRENTRY(of->of_vnode, &kuio);

	if (result == 0) {
		/*
		 * Persist the updated slot index back into the fd so
		 * the next call continues from where we left off.
		 * sfs_getdirentry sets uio_offset to slot+1.
		 */
		of->of_offset = kuio.uio_offset;
	}

	of_release(of);

	if (result) {
		return result;
	}

	/* Return the number of bytes actually read (typically strlen+1). */
	*retval = (int32_t)(buflen - kuio.uio_resid);
	return 0;
}
