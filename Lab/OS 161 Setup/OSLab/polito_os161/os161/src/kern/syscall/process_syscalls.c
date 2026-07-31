/*
 * Minimal process-related system calls.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <copyinout.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <vfs.h>
#include <syscall.h>

#define MAX_EXEC_ARGS 128

static
vaddr_t
align_down(vaddr_t val, unsigned align)
{
	return val & ~((vaddr_t)align - 1);
}

int
sys_getpid(int32_t *retval)
{
	*retval = proc_getpid();
	return 0;
}

int
sys_waitpid(pid_t pid, userptr_t status, int options, int32_t *retval)
{
	return proc_wait(pid, status, options, retval);
}

int
sys_fork(struct trapframe *tf, int32_t *retval)
{
	struct trapframe *childtf;
	struct addrspace *childas;
	struct proc *childproc;
	int result;

	childtf = kmalloc(sizeof(*childtf));
	if (childtf == NULL) {
		return ENOMEM;
	}
	memcpy(childtf, tf, sizeof(*childtf));

	result = as_copy(proc_getas(), &childas);
	if (result) {
		kfree(childtf);
		return result;
	}

	childproc = proc_create_fork(curproc->p_name, curproc, childas);
	if (childproc == NULL) {
		as_destroy(childas);
		kfree(childtf);
		return ENOMEM;
	}

	result = thread_fork(curthread->t_name, childproc, enter_forked_process,
			     childtf, 0);
	if (result) {
		proc_destroy(childproc);
		kfree(childtf);
		return result;
	}

	*retval = childproc->p_pid;
	return 0;
}

static
void
free_args(char **argv, int argc)
{
	int i;

	if (argv == NULL) {
		return;
	}
	for (i = 0; i < argc; i++) {
		kfree(argv[i]);
	}
	kfree(argv);
}

static
int
copyin_args(userptr_t uargv, char ***argv_ret, int *argc_ret)
{
	char *tmp;
	char **argv;
	userptr_t argptr;
	size_t total, got;
	int argc, result;

	tmp = kmalloc(PATH_MAX);
	if (tmp == NULL) {
		return ENOMEM;
	}
	argv = kmalloc(sizeof(char *) * (MAX_EXEC_ARGS + 1));
	if (argv == NULL) {
		kfree(tmp);
		return ENOMEM;
	}

	total = 0;
	for (argc = 0; argc < MAX_EXEC_ARGS; argc++) {
		argv[argc] = NULL;
		result = copyin((const_userptr_t)((vaddr_t)uargv +
				  argc * sizeof(userptr_t)), &argptr,
				  sizeof(argptr));
		if (result) {
			free_args(argv, argc);
			kfree(tmp);
			return result;
		}
		if (argptr == NULL) {
			argv[argc] = NULL;
			*argv_ret = argv;
			*argc_ret = argc;
			kfree(tmp);
			return 0;
		}

		result = copyinstr((const_userptr_t)argptr, tmp, PATH_MAX, &got);
		if (result == ENAMETOOLONG) {
			result = E2BIG;
		}
		if (result) {
			free_args(argv, argc);
			kfree(tmp);
			return result;
		}

		total += (got + 3) & ~(size_t)3;
		if (total > ARG_MAX) {
			free_args(argv, argc);
			kfree(tmp);
			return E2BIG;
		}

		argv[argc] = kstrdup(tmp);
		if (argv[argc] == NULL) {
			free_args(argv, argc);
			kfree(tmp);
			return ENOMEM;
		}
	}

	free_args(argv, argc);
	kfree(tmp);
	return E2BIG;
}

int
copyout_args(char **argv, int argc, vaddr_t *stackptr_ret, userptr_t *uargv_ret)
{
	userptr_t *uargv;
	userptr_t nullarg;
	vaddr_t stackptr;
	size_t len;
	int i, result;

	uargv = kmalloc(sizeof(userptr_t) * (argc + 1));
	if (uargv == NULL) {
		return ENOMEM;
	}

	stackptr = *stackptr_ret;
	for (i = argc - 1; i >= 0; i--) {
		len = strlen(argv[i]) + 1;
		stackptr -= len;
		stackptr = align_down(stackptr, 4);
		result = copyoutstr(argv[i], (userptr_t)stackptr, len, NULL);
		if (result) {
			kfree(uargv);
			return result;
		}
		uargv[i] = (userptr_t)stackptr;
	}
	uargv[argc] = NULL;

	nullarg = NULL;
	stackptr = align_down(stackptr, 8);
	stackptr -= sizeof(userptr_t);
	result = copyout(&nullarg, (userptr_t)stackptr, sizeof(nullarg));
	if (result) {
		kfree(uargv);
		return result;
	}

	for (i = argc - 1; i >= 0; i--) {
		stackptr -= sizeof(userptr_t);
		result = copyout(&uargv[i], (userptr_t)stackptr, sizeof(uargv[i]));
		if (result) {
			kfree(uargv);
			return result;
		}
	}

	*uargv_ret = (userptr_t)stackptr;
	*stackptr_ret = stackptr;
	kfree(uargv);
	return 0;
}

int
sys_execv(const_userptr_t progname, userptr_t uargv)
{
	char *kprog;
	char **argv;
	struct addrspace *oldas, *newas;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	userptr_t argv_userptr;
	int argc, result;

	if (progname == NULL || uargv == NULL) {
		return EFAULT;
	}

	kprog = kmalloc(PATH_MAX);
	if (kprog == NULL) {
		return ENOMEM;
	}
	result = copyinstr(progname, kprog, PATH_MAX, NULL);
	if (result) {
		kfree(kprog);
		return result;
	}

	result = copyin_args(uargv, &argv, &argc);
	if (result) {
		kfree(kprog);
		return result;
	}

	result = vfs_open(kprog, O_RDONLY, 0, &v);
	if (result) {
		free_args(argv, argc);
		kfree(kprog);
		return result;
	}

	newas = as_create();
	if (newas == NULL) {
		vfs_close(v);
		free_args(argv, argc);
		kfree(kprog);
		return ENOMEM;
	}

	oldas = proc_setas(newas);
	as_activate();

	result = load_elf(v, &entrypoint);
	vfs_close(v);
	if (result) {
		proc_setas(oldas);
		as_activate();
		as_destroy(newas);
		free_args(argv, argc);
		kfree(kprog);
		return result;
	}

	result = as_define_stack(newas, &stackptr);
	if (result) {
		proc_setas(oldas);
		as_activate();
		as_destroy(newas);
		free_args(argv, argc);
		kfree(kprog);
		return result;
	}

	result = copyout_args(argv, argc, &stackptr, &argv_userptr);
	if (result) {
		proc_setas(oldas);
		as_activate();
		as_destroy(newas);
		free_args(argv, argc);
		kfree(kprog);
		return result;
	}

	as_destroy(oldas);
	free_args(argv, argc);
	kfree(kprog);

	enter_new_process(argc, argv_userptr, NULL, stackptr, entrypoint);
	panic("enter_new_process returned\n");
	return EINVAL;
}