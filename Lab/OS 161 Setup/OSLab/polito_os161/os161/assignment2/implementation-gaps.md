# Implementation Gaps and Risk Areas

## Highest-Priority Incomplete Code

### Synchronization

Files:

- `src/kern/thread/synch.c`
- `src/kern/include/synch.h`

Current state:

- Semaphores are implemented.
- Locks are stubs.
- CVs are stubs.
- `lock_do_i_hold` returns `true`, which can hide bugs and makes code believe arbitrary locks are held.

Impact:

- `sy2`, `sy3`, and `sy4` should fail or behave incorrectly.
- Any kernel subsystem using `struct lock` or `struct cv` cannot be trusted until this is fixed.
- `semfs` uses locks/CVs and depends on correct implementations.

### Syscalls and Process Lifecycle

Files:

- `src/kern/arch/mips/syscall/syscall.c`
- `src/kern/syscall/runprogram.c`
- `src/kern/proc/proc.c`
- `src/kern/include/proc.h`
- `src/kern/include/syscall.h`

Current state:

- Dispatcher handles only `reboot` and `__time`.
- `enter_forked_process` is empty.
- No full fork, execv, waitpid, exit, getpid, file descriptor, cwd, or sbrk syscall implementation is wired in.
- Process tracking is minimal and lacks PID/parent/child/exit-status handling.
- Menu-launched user programs do not pass arguments and are not waited for.

Impact:

- Most userland commands/tests that rely on normal Unix syscalls will return `ENOSYS` or fail.
- Shell usability is limited until process and file syscalls exist.

### Virtual Memory

Files:

- `src/kern/arch/mips/vm/dumbvm.c`
- `src/kern/vm/addrspace.c`
- `src/kern/include/addrspace.h`
- `src/kern/include/vm.h`

Current state:

- DUMBVM is active and intentionally limited.
- `src/kern/vm/addrspace.c` is mostly assignment stub code.
- `free_kpages` under DUMBVM leaks memory.
- Page permissions are ignored.
- Only two regions plus fixed stack are supported.
- TLB exhaustion is fatal to the process path.

Impact:

- VM stress tests and robust multiprogramming will fail until a real VM subsystem exists.
- `sbrk`/malloc growth behavior depends on future VM work.

## Filesystem and VFS Limitations

### SFS

Files:

- `src/kern/fs/sfs/*`

Current state:

- Basic flat-file SFS is implemented.
- Subdirectories are not supported.
- Many advanced vnode operations return `ENOSYS` through explicit stubs or `vfsfail`.
- VFS/SFS relies heavily on a coarse recursive `vfs_biglock`.

Impact:

- Directory-tree tests beyond flat root-directory behavior will fail.
- Concurrency is simple but not scalable.

### emufs

Files:

- `src/kern/dev/lamebus/emu.c`

Current state:

- Host-backed emufs support is present.
- Several operations are intentionally unsupported and return `ENOSYS`.

Impact:

- Good for bootstrapping and host file access, but not a complete Unix filesystem.

## Configuration Limits

Files:

- `src/kern/conf/DUMBVM`
- `src/kern/conf/GENERIC`
- `src/kern/arch/mips/conf/conf.arch`

Current state:

- DUMBVM config is active-style.
- Network and screen console options are present but commented or unsupported.
- Build output in `src/kern/compile/DUMBVM/` is generated from config.

Impact:

- Do not edit generated `compile/DUMBVM` files as source.
- Enable features through config/source, then regenerate/rebuild.

## Generated/Binary Areas to Avoid Editing

Avoid treating these as source of truth:

- `src/build/`
- `src/kern/compile/DUMBVM/`
- `root/bin`, `root/sbin`, `root/testbin`, `root/lib`, `root/kernel-DUMBVM`
- `tools/`
- `root/*.img`

Edit source under `src/`, rebuild, then reinstall/run as needed.

## Suggested Assignment Order

1. Implement locks and CVs in `synch.c`/`synch.h`.
2. Add process IDs, exit/wait lifecycle, and basic syscall dispatch.
3. Implement file descriptor table and file syscalls.
4. Implement argument passing for `runprogram`/`execv`.
5. Replace DUMBVM with the assignment VM design.
6. Expand SFS/VFS behavior only if the assignment requires it.
