# Process Syscalls Implementation

This note documents the implementation added after the shell reached:

```text
Unknown syscall 0
(program name unknown): fork: Function not implemented
```

`SYS_fork` is syscall number `0`, and the shell runs external commands with this sequence:

```c
pid = fork();
if (pid == 0) {
        execvp(args[0], args);
        _exit(1);
}
waitpid(pid, &status, 0);
```

To support `/testbin/add 2 3` from the shell, the kernel needs at least `fork`, `execv`, `waitpid`, `getpid`, argument copying for `execv`, and a way for the kernel menu to wait while the shell owns the console.

## File 1: `src/kern/include/proc.h`

Added a forward declaration:

```c
struct semaphore;
```

Added process fields:

```c
pid_t p_pid;
struct proc *p_parent;
bool p_exited;
bool p_waited;
int p_exitstatus;
struct semaphore *p_waitsem;
```

Added helper declarations:

```c
struct proc *proc_create_fork(const char *name, struct proc *parent,
                              struct addrspace *as);
pid_t proc_getpid(void);
void proc_exit(int status);
int proc_wait(pid_t pid, userptr_t statusptr, int options, int32_t *retval);
```

Why: the stock process structure had no PID, parent pointer, exit state, or wait primitive.

## File 2: `src/kern/proc/proc.c`

Added a small PID table:

```c
#define PID_MAX 128
static struct spinlock proctable_lock = SPINLOCK_INITIALIZER;
static struct proc *proctable[PID_MAX];
static pid_t nextpid = 1;
```

Added PID registration, fork process creation, exit marking, and wait cleanup:

```c
static pid_t proc_allocpid(void);
static int proc_register(struct proc *proc, struct proc *parent);
struct proc *proc_create_fork(const char *name, struct proc *parent,
                              struct addrspace *as);
void proc_exit(int status);
int proc_wait(pid_t pid, userptr_t statusptr, int options, int32_t *retval);
```

Why: `fork` needs a child process with its own PID, `_exit` needs to record wait status, and `waitpid` needs to block until a specific child exits.

## File 3: `src/kern/thread/thread.c`

Inside `thread_exit`, the process is saved before detach:

```c
struct proc *proc;

cur = curthread;
proc = cur->t_proc;
```

After `proc_remthread(cur)`, the waiter is woken:

```c
if (proc != NULL && proc->p_exited && proc->p_waitsem != NULL) {
        V(proc->p_waitsem);
}
```

Why: the parent should not wake and destroy the child process until the child thread has detached from it.

## File 4: `src/kern/syscall/process_syscalls.c`

Added a new syscall implementation file with:

```c
int sys_getpid(int32_t *retval);
int sys_waitpid(pid_t pid, userptr_t status, int options, int32_t *retval);
int sys_fork(struct trapframe *tf, int32_t *retval);
int sys_execv(const_userptr_t progname, userptr_t uargv);
```

`sys_fork` copies the current trapframe and address space, creates a child process, starts a child thread, and returns the child PID to the parent.

`sys_execv` copies the program path and argument strings from the old address space into kernel memory, loads the new executable, copies strings and the `argv[]` pointer table onto the new user stack, and calls:

```c
enter_new_process(argc, argv_userptr, NULL, stackptr, entrypoint);
```

Why: `/testbin/add 2 3` needs `argc == 3` and valid `argv[0..2]` in the new process.

## File 5: `src/kern/arch/mips/syscall/syscall.c`

Added dispatcher cases:

```c
case SYS_fork:
        err = sys_fork(tf, &retval);
        break;

case SYS_execv:
        err = sys_execv((const_userptr_t)tf->tf_a0,
                        (userptr_t)tf->tf_a1);
        break;

case SYS_waitpid:
        err = sys_waitpid((pid_t)tf->tf_a0, (userptr_t)tf->tf_a1,
                          (int)tf->tf_a2, &retval);
        break;

case SYS_getpid:
        err = sys_getpid(&retval);
        break;
```

Implemented child return from `fork`:

```c
childtf.tf_v0 = 0;
childtf.tf_a3 = 0;
childtf.tf_epc += 4;
mips_usermode(&childtf);
```

Why: parent returns child PID; child returns `0`; both must advance past the syscall instruction.

## File 6: `src/kern/include/syscall.h`

Added prototypes:

```c
int sys_fork(struct trapframe *tf, int32_t *retval);
int sys_execv(const_userptr_t progname, userptr_t argv);
int sys_waitpid(pid_t pid, userptr_t status, int options, int32_t *retval);
int sys_getpid(int32_t *retval);
```

Why: the dispatcher needs declarations for the new syscall implementations.

## File 7: `src/kern/conf/conf.kern`

Added:

```text
file      syscall/process_syscalls.c
```

Why: the new syscall implementation file must be compiled into the configured kernel.

## File 8: `src/kern/main/menu.c`

After launching a user program, the kernel menu now waits:

```c
result = proc_wait(proc->p_pid, NULL, 0, NULL);
if (result) {
        kprintf("waitpid failed: %s\n", strerror(result));
        return result;
}
```

Why: previously `s` started the shell thread and immediately returned to the kernel menu, so the shell and kernel menu both read the console. Waiting gives the shell exclusive console input until it exits.

## Rebuild Steps

Because `conf.kern` changed, rerun config:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/src/kern/conf"
./config DUMBVM
cd ../compile/DUMBVM
bmake clean
bmake depend
bmake
bmake install
```

Then boot:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
../tools/bin/sys161 kernel-DUMBVM
```

At the kernel prompt:

```text
s
```

At the shell prompt:

```text
/testbin/add 2 3
```

Expected output:

```text
Answer: 5
```

## Remaining Limitations

This is still not a full OS/161 process system:

- no general file descriptor table;
- no `open`, `close`, `lseek`, or file-backed `read/write`;
- no `chdir` syscall for shell built-in directory changes;
- no orphan reparenting;
- no signal handling;
- PID table is intentionally small and simple.

It is enough for the basic shell external-command path when the program only needs console I/O and process lifecycle syscalls.
## Build Fix: `thread_fork` Callback Type

The rebuild failed with:

```text
error: passing argument 3 of 'thread_fork' from incompatible pointer type
expected 'void (*)(void *, long unsigned int)' but argument is of type 'void (*)(struct trapframe *)'
```

### File: `src/kern/include/syscall.h`

Changed the fork child-entry prototype from:

```c
void enter_forked_process(struct trapframe *tf);
```

to:

```c
void enter_forked_process(void *tf, unsigned long unused);
```

Why: `thread_fork` always calls thread entry functions with two arguments, `void *data1` and `unsigned long data2`.

### File: `src/kern/arch/mips/syscall/syscall.c`

Changed the implementation to match `thread_fork`:

```c
void
enter_forked_process(void *data1, unsigned long unused)
{
        struct trapframe *tf = data1;
        struct trapframe childtf;

        (void)unused;

        memcpy(&childtf, tf, sizeof(childtf));
        kfree(tf);

        childtf.tf_v0 = 0;
        childtf.tf_a3 = 0;
        childtf.tf_epc += 4;

        mips_usermode(&childtf);
        panic("mips_usermode returned\n");
}
```

Why: the child process still receives the copied trapframe, but the entry function now has the exact callback shape required by `thread_fork`, so the kernel can compile with `-Werror`.
## Runtime Fix: `/testbin/add` Out of Memory

After the process syscalls compiled, running from the shell reached `execv` but failed with:

```text
OS/161$ /testbin/add 2 3
(program name unknown): /testbin/add: Out of memory
Exit 1
```

Cause: DUMBVM has very little RAM and does not actually free physical user pages in `free_kpages`. The shell path is:

```c
fork();
execv("/testbin/add", argv);
```

That means memory is needed for the shell, the forked copy of the shell, and then the new `add` address space. The previous `execv` code also allocated a full `ARG_MAX` buffer, which is 64 KB, even when the command only had short arguments like `2` and `3`.

### File: `src/kern/syscall/process_syscalls.c`

Changed argument copying from one large fixed buffer:

```c
argbuf = kmalloc(ARG_MAX);
```

to a smaller per-argument approach:

```c
tmp = kmalloc(PATH_MAX);
...
argv[argc] = kstrdup(tmp);
```

Also added:

```c
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
```

Why: this keeps `execv` memory proportional to the actual arguments instead of always reserving 64 KB.

### File: `src/kern/arch/mips/vm/dumbvm.c`

Changed:

```c
#define DUMBVM_STACKPAGES    18
```

to:

```c
#define DUMBVM_STACKPAGES    4
```

Why: DUMBVM gives every process a fixed stack. With 18 pages, every shell, forked child, and executed program consumes 72 KB for stack alone. Four pages gives a 16 KB stack, which is enough for simple `/testbin/add 2 3` testing and makes fork/exec fit in the current low-RAM System/161 setup.

Tradeoff: this is a pragmatic DUMBVM-only lab fix. Large argument tests near `ARG_MAX` need more RAM or a real VM implementation with reclaimable pages.