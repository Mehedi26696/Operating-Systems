/*
 * Copyright (c) 2013
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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <limits.h>

struct addrspace;
struct thread;
struct vnode;
struct semaphore;

/*
 * File descriptor table support.
 *
 * Each open file is represented by an `openfile` that pairs a vnode
 * with an offset and the open flags. A process holds up to
 * OPEN_MAX file descriptors in its fd table.
 */
struct openfile {
	struct vnode *of_vnode;   /* the vnode for this open file */
	off_t of_offset;          /* current file position */
	int of_flags;             /* open flags (O_RDONLY/O_WRONLY/O_RDWR/O_APPEND) */
	int of_refcount;          /* number of fd table entries pointing here */
};

/* File descriptor table size. */
#define FD_MAX 32

struct fdarray {
	struct openfile *fd_table[FD_MAX];
};

/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	unsigned p_numthreads;		/* Number of threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */
	struct fdarray p_fdtable;	/* file descriptor table */

	/* Process identity and parent/child state. */
	pid_t p_pid;
	struct proc *p_parent;
	bool p_exited;
	bool p_waited;
	int p_exitstatus;
	struct semaphore *p_waitsem;
};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Create a child process for fork with an already-copied address space. */
struct proc *proc_create_fork(const char *name, struct proc *parent,
                              struct addrspace *as);

/* Process table helpers for syscall implementations. */
pid_t proc_getpid(void);
void proc_exit(int status);
int proc_wait(pid_t pid, userptr_t statusptr, int options, int32_t *retval);
/* Destroy a process. */
void proc_destroy(struct proc *proc);

/*
 * File descriptor table helpers (defined in proc.c, used by syscall code).
 *
 *   fdinit      - Initialize the FD table for a newly created process.
 *   fddup       - Duplicate an open file (increments refcount).
 *   fddestroy   - Release all fds held by a process.
 *   fdalloc     - Allocate a new fd slot and install the given openfile.
 *                 Returns the fd number (>= 0) on success or an error code.
 *   fdget       - Look up an openfile from an fd and acquire a reference.
 *                 Must be paired with an of_release() when done.
 *                 Returns 0 on success and EBADF on missing fd.
 *   of_release  - Drop an openfile reference acquired via fdget. Frees
 *                 the openfile (and decrefs the vnode) if refcount==0.
 *   fdclose     - Clear the fd slot, drop the slot's reference.
 */
void fdinit(struct proc *proc);
int fdalloc(struct proc *proc, struct openfile *of, int *retval);
int fdget(struct proc *proc, int fd, struct openfile **ret);
void fddup(struct openfile *of);
void of_release(struct openfile *of);
void fdclose(struct proc *proc, int fd);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);


#endif /* _PROC_H_ */
