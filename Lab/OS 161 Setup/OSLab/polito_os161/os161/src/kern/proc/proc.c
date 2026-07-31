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

/*
 * Process support.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <spl.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <synch.h>
#include <copyinout.h>

#define PROC_PID_MAX 128

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

static struct spinlock proctable_lock = SPINLOCK_INITIALIZER;
static struct proc *proctable[PROC_PID_MAX];
static pid_t nextpid = 1;

/* Forward declaration: defined below; called from proc_exit. */
static void fd_destroy_all(struct proc *proc);

static
pid_t
proc_allocpid(void)
{
	pid_t start, pid;

	start = nextpid;
	pid = start;
	do {
		if (proctable[pid] == NULL) {
			nextpid = pid + 1;
			if (nextpid >= PROC_PID_MAX) {
				nextpid = 1;
			}
			return pid;
		}
		pid++;
		if (pid >= PROC_PID_MAX) {
			pid = 1;
		}
	} while (pid != start);

	return -1;
}

static
int
proc_register(struct proc *proc, struct proc *parent)
{
	pid_t pid;

	proc->p_waitsem = sem_create("proc-wait", 0);
	if (proc->p_waitsem == NULL) {
		return ENOMEM;
	}

	spinlock_acquire(&proctable_lock);
	pid = proc_allocpid();
	if (pid < 0) {
		spinlock_release(&proctable_lock);
		sem_destroy(proc->p_waitsem);
		proc->p_waitsem = NULL;
		return ENPROC;
	}

	proc->p_pid = pid;
	proc->p_parent = parent;
	proc->p_exited = false;
	proc->p_waited = false;
	proc->p_exitstatus = 0;
	proctable[pid] = proc;
	spinlock_release(&proctable_lock);

	return 0;
}

static
void
proc_inherit_cwd(struct proc *newproc, struct proc *oldproc)
{
	if (oldproc == NULL) {
		return;
	}

	spinlock_acquire(&oldproc->p_lock);
	if (oldproc->p_cwd != NULL) {
		VOP_INCREF(oldproc->p_cwd);
		newproc->p_cwd = oldproc->p_cwd;
	}
	spinlock_release(&oldproc->p_lock);
}

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;
	fdinit(proc);

	/* Process table fields */
	proc->p_pid = 0;
	proc->p_parent = NULL;
	proc->p_exited = false;
	proc->p_waited = false;
	proc->p_exitstatus = 0;
	proc->p_waitsem = NULL;

	return proc;
}

void
proc_destroy(struct proc *proc)
{
	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	spinlock_acquire(&proctable_lock);
	if (proc->p_pid > 0 && proc->p_pid < PROC_PID_MAX &&
	    proctable[proc->p_pid] == proc) {
		proctable[proc->p_pid] = NULL;
	}
	spinlock_release(&proctable_lock);

	if (proc->p_waitsem != NULL) {
		sem_destroy(proc->p_waitsem);
		proc->p_waitsem = NULL;
	}

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}

void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;
	int result;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	result = proc_register(newproc, curproc);
	if (result) {
		proc_destroy(newproc);
		return NULL;
	}

	proc_inherit_cwd(newproc, curproc);
	return newproc;
}

struct proc *
proc_create_fork(const char *name, struct proc *parent, struct addrspace *as)
{
	struct proc *newproc;
	int result;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	result = proc_register(newproc, parent);
	if (result) {
		proc_destroy(newproc);
		return NULL;
	}

	newproc->p_addrspace = as;
	proc_inherit_cwd(newproc, parent);
	return newproc;
}

pid_t
proc_getpid(void)
{
	if (curproc == NULL) {
		return 0;
	}
	return curproc->p_pid;
}

void
proc_exit(int status)
{
	struct proc *proc = curproc;

	if (proc == NULL) {
		thread_exit();
	}

	/* Release all file descriptors held by this process. */
	fd_destroy_all(proc);

	spinlock_acquire(&proc->p_lock);
	proc->p_exitstatus = _MKWAIT_EXIT(status & 0xff);
	proc->p_exited = true;
	/*
	 * Note: the parent is woken by V(p_waitsem) inside thread_exit()
	 * once p_exited has been observed there. We don't V here because
	 * the parent could otherwise race in between p_exited=true and
	 * our V, see p_exited=true, run proc_destroy(), and free the
	 * sem out from under us.
	 */
	spinlock_release(&proc->p_lock);
}

int
proc_wait(pid_t pid, userptr_t statusptr, int options, int32_t *retval)
{
	struct proc *child;
	int status, result;
	bool exited;

	if ((options & ~WALLFLAGS) != 0) {
		return EINVAL;
	}
	if (pid <= 0 || pid >= PROC_PID_MAX) {
		return ECHILD;
	}

	spinlock_acquire(&proctable_lock);
	child = proctable[pid];
	if (child == NULL || child->p_parent != curproc || child->p_waited) {
		spinlock_release(&proctable_lock);
		return ECHILD;
	}

	spinlock_acquire(&child->p_lock);
	exited = child->p_exited;
	if (!exited && options == WNOHANG) {
		spinlock_release(&child->p_lock);
		spinlock_release(&proctable_lock);
		if (retval != NULL) {
			*retval = 0;
		}
		return 0;
	}
	child->p_waited = true;
	spinlock_release(&child->p_lock);
	spinlock_release(&proctable_lock);

	if (!exited) {
		P(child->p_waitsem);
	}

	spinlock_acquire(&child->p_lock);
	status = child->p_exitstatus;
	spinlock_release(&child->p_lock);

	if (statusptr != NULL) {
		result = copyout(&status, statusptr, sizeof(status));
		if (result) {
			return result;
		}
	}

	if (retval != NULL) {
		*retval = pid;
	}
	proc_destroy(child);
	return 0;
}

int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

/*
 * Initialize an FD table to empty.
 */
void
fdinit(struct proc *proc)
{
	int i;

	KASSERT(proc != NULL);
	for (i = 0; i < FD_MAX; i++) {
		proc->p_fdtable.fd_table[i] = NULL;
	}
}

/*
 * Allocate a new fd slot pointing at openfile of. Returns the fd
 * number in *retval on success.
 */
int
fdalloc(struct proc *proc, struct openfile *of, int *retval)
{
	int fd;

	KASSERT(proc != NULL);
	KASSERT(of != NULL);

	/*
	 * fds 0, 1, 2 (STDIN/STDOUT/STDERR) are reserved for the console
	 * and are never stored in the table; sys_read/sys_write route those
	 * fd numbers to console_read/console_write. Start allocating at 3 so
	 * an opened file never collides with a console fd -- otherwise a
	 * later read(0, ...) on an opened file would be misrouted to the
	 * console and block forever waiting for keyboard input.
	 */
	for (fd = 3; fd < FD_MAX; fd++) {
		if (proc->p_fdtable.fd_table[fd] == NULL) {
			proc->p_fdtable.fd_table[fd] = of;
			*retval = fd;
			return 0;
		}
	}
	return EMFILE;
}

/*
 * Look up an fd in the table and hand back the openfile, taking a
 * reference to it. The caller must later release the reference via
 * of_release().
 */
int
fdget(struct proc *proc, int fd, struct openfile **ret)
{
	struct openfile *of;

	KASSERT(proc != NULL);
	KASSERT(ret != NULL);

	if (fd < 0 || fd >= FD_MAX) {
		return EBADF;
	}
	of = proc->p_fdtable.fd_table[fd];
	if (of == NULL) {
		return EBADF;
	}
	of->of_refcount++;
	*ret = of;
	return 0;
}

/*
 * Increment an openfile's refcount (used by dup2 to share the openfile).
 */
void
fddup(struct openfile *of)
{
	KASSERT(of != NULL);
	of->of_refcount++;
}

/*
 * Release a reference to an openfile. Frees it (and decrefs the vnode)
 * if refcount drops to zero.
 */
void
of_release(struct openfile *of)
{
	KASSERT(of != NULL);
	KASSERT(of->of_refcount > 0);
	of->of_refcount--;
	if (of->of_refcount == 0) {
		vfs_close(of->of_vnode);
		kfree(of);
	}
}

/*
 * Close an fd slot. Drops the slot's reference via of_release.
 */
void
fdclose(struct proc *proc, int fd)
{
	struct openfile *of;

	KASSERT(proc != NULL);

	if (fd < 0 || fd >= FD_MAX) {
		return;
	}
	of = proc->p_fdtable.fd_table[fd];
	if (of == NULL) {
		return;
	}
	proc->p_fdtable.fd_table[fd] = NULL;
	of_release(of);
}

/*
 * Close every fd held by the process; used by _exit/sys_exit.
 */
static
void
fd_destroy_all(struct proc *proc)
{
	int fd;

	for (fd = 0; fd < FD_MAX; fd++) {
		fdclose(proc, fd);
	}
}