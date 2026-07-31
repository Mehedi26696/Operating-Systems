/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYSCALL_H_
#define _SYSCALL_H_


#include <cdefs.h> /* for __DEAD */
#include <types.h>
struct trapframe; /* from <machine/trapframe.h> */

/*
 * The system call dispatcher.
 */

void syscall(struct trapframe *tf);

/*
 * Support functions.
 */

/* Helper for fork(). */
void enter_forked_process(void *tf, unsigned long unused);

/* Enter user mode. Does not return. */
__DEAD void enter_new_process(int argc, userptr_t argv, userptr_t env,
		       vaddr_t stackptr, vaddr_t entrypoint);

/* Console helpers exposed for use by file_syscalls.c. */
int console_read(userptr_t buf, size_t buflen, int32_t *retval);
int console_write(const_userptr_t buf, size_t nbytes, int32_t *retval);


/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);
int sys___time(userptr_t user_seconds, userptr_t user_nanoseconds);
int sys_fork(struct trapframe *tf, int32_t *retval);
int sys_execv(const_userptr_t progname, userptr_t argv);

/*
 * Copy an in-kernel argv array onto a freshly-defined user stack, laying
 * out the argument strings and the NULL-terminated pointer vector the way
 * the userland crt0 expects. Shared by sys_execv and runprogram.
 */
int copyout_args(char **argv, int argc, vaddr_t *stackptr, userptr_t *uargv);
int sys_waitpid(pid_t pid, userptr_t status, int options, int32_t *retval);
int sys_getpid(int32_t *retval);
int sys_read(int fd, userptr_t buf, size_t buflen, int32_t *retval);
int sys_write(int fd, const_userptr_t buf, size_t nbytes, int32_t *retval);
__DEAD void sys__exit(int code);

/*
 * Assignment 2 file-system syscalls.
 */
int sys_open(const_userptr_t path, int flags, mode_t mode, int32_t *retval);
int sys_close(int fd, int32_t *retval);
int sys_lseek(int fd, off_t pos, int whence, int32_t *retval);
int sys_dup2(int oldfd, int newfd, int32_t *retval);
int sys_chdir(const_userptr_t path, int32_t *retval);
int sys___getcwd(userptr_t buf, size_t buflen, int32_t *retval);
int sys_mkdir(const_userptr_t path, mode_t mode, int32_t *retval);
int sys_rmdir(const_userptr_t path, int32_t *retval);
int sys_link(const_userptr_t oldpath, const_userptr_t newpath,
	     int32_t *retval);
int sys_unlink(const_userptr_t path, int32_t *retval);
int sys_symlink(const_userptr_t contents, const_userptr_t path,
		int32_t *retval);
int sys_readlink(const_userptr_t path, userptr_t buf, size_t buflen,
		 int32_t *retval);
int sys_fstat(int fd, userptr_t statbuf, int32_t *retval);
int sys_stat(const_userptr_t path, userptr_t statbuf, int32_t *retval);
int sys_lstat(const_userptr_t path, userptr_t statbuf, int32_t *retval);
int sys_getdirentry(int fd, userptr_t buf, size_t buflen, int32_t *retval);
#endif /* _SYSCALL_H_ */
