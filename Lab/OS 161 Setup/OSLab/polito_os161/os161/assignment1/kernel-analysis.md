# Kernel Analysis

## Boot and Main Flow

The kernel entry path is `src/kern/main/main.c`. `kmain` calls `boot`, then enters the interactive kernel menu. Boot order is deliberately staged:

1. RAM, process, thread, hardclock, VFS, and heap generation setup.
2. Device probe through `mainbus_bootstrap`, then pseudodevice configuration.
3. VM bootstrap, kprintf bootstrap, secondary CPU startup, and default boot filesystem setup.

The default boot filesystem is set to `emu0` if present. Shutdown clears VFS state, unmounts filesystems, shuts down threads, and raises IPL.

## Menu and Kernel Test Harness

`src/kern/main/menu.c` implements a semicolon-aware command interpreter. It supports:

- shell/program launch through `s` and `p`;
- VFS operations such as `mount`, `unmount`, `bootfs`, `cd`, `pwd`, `sync`;
- debug/panic/deadlock commands;
- kernel heap diagnostics;
- built-in tests for arrays, bitmaps, threads, semaphores, locks, CVs, VFS/SFS, and malloc.

Program launch creates a process and forks a thread that calls `runprogram`. Argument passing from the menu is explicitly not implemented.

## Threads and Scheduling

Thread logic lives mainly in `src/kern/thread/`.

- `thread.c` handles creation, fork, exit, sleep, wakeup, migration, and context switch integration.
- `threadlist.c` provides list operations.
- `wchan` support is used for blocking synchronization.
- `spinlock.c`, `spl.c`, and arch-specific interrupt paths provide low-level exclusion.
- `clock.c` drives timer handling.

The thread base is present and functional enough for the stock OS/161 tests.

## Synchronization

`src/kern/thread/synch.c` has a complete semaphore implementation:

- `sem_create` creates a wait channel and spinlock.
- `P` sleeps while the count is zero.
- `V` increments and wakes one waiter.

Locks and condition variables are assignment stubs:

- `struct lock` in `src/kern/include/synch.h` has no owner, wait channel, or state fields.
- `lock_create`, `lock_destroy`, `lock_acquire`, `lock_release`, and `lock_do_i_hold` are incomplete.
- `lock_do_i_hold` currently always returns `true`.
- `struct cv` has no wait channel/state fields.
- `cv_wait`, `cv_signal`, and `cv_broadcast` are empty stubs.

This means tests `sy2`, `sy3`, `sy4`, and any subsystem relying on real locks/CVs are not semantically correct until implemented.

## Process Model

`src/kern/proc/proc.c` provides a minimal process abstraction:

- `kproc` represents kernel-only threads.
- `proc_create_runprogram` creates a process for user program execution and inherits current directory.
- `proc_addthread` and `proc_remthread` maintain process membership.
- `proc_getas` and `proc_setas` manage the process address-space pointer.

The file comments explicitly expect later extension for exit/wait, multithreaded user processes, and address-space lifetime. There is no full PID table, parent/child tracking, exit status handling, file descriptor table, or wait lifecycle yet.

## Syscalls

The MIPS syscall dispatcher is `src/kern/arch/mips/syscall/syscall.c`.

Currently dispatched:

- `SYS_reboot`
- `SYS___time`

All other syscalls fall through to `ENOSYS`. `enter_forked_process` is also a placeholder. Userland has syscall stubs and many tests for file/process syscalls, but the kernel side is mostly not connected.

`src/kern/syscall/runprogram.c` loads an executable and enters user mode. It is reference code for `execv`, but does not implement argument passing or process replacement semantics.

## Virtual Memory

The active configuration includes DUMBVM via `src/kern/arch/mips/vm/dumbvm.c`.

DUMBVM behavior:

- physical memory is allocated through `ram_stealmem`;
- `free_kpages` leaks memory;
- address spaces support at most two ELF regions plus a fixed 18-page user stack;
- page permissions are ignored and pages are mapped read/write;
- TLB refill scans for a free TLB slot and fails when all entries are full;
- TLB shootdown panics because multiprocessor VM support is absent.

`src/kern/vm/addrspace.c` is the assignment-facing replacement surface and is largely stubbed with `Write this` comments and `ENOSYS`.

## VFS

The VFS layer lives under `src/kern/vfs/`.

- `vfslist.c` tracks devices/filesystems and implements a recursive `vfs_biglock`.
- `vfslookup.c` resolves paths from bootfs/current directory and delegates to vnode ops.
- `vfspath.c` implements high-level open, close, remove, rename, link, mkdir, rmdir, and readlink helpers.
- `vnode.c` handles vnode refcounts and vnode operation validation.
- `device.c` and `devnull.c` provide device vnode integration.
- `vfsfail.c` supplies default failure implementations for unsupported vnode ops.

The VFS core is relatively complete for the base system, but uses coarse locking.

## Filesystems

### SFS

`src/kern/fs/sfs/` implements the Simple File System.

- `sfs_fsops.c`: mount, unmount, sync, freemap setup.
- `sfs_inode.c`: vnode/inode lifecycle.
- `sfs_io.c`: file I/O.
- `sfs_bmap.c` and `sfs_balloc.c`: block mapping/allocation.
- `sfs_dir.c`: flat directory operations.
- `sfs_vnops.c`: vnode operation table for files/directories.

Important limitation: stock SFS in this tree does not support subdirectories; directory vnode ops route many directory features to `ENOSYS`.

### semfs

`src/kern/fs/semfs/` implements a semaphore filesystem with vnode-backed named semaphores. It uses locks/CVs internally, so its correctness depends on the synchronization assignment being completed.

## Devices and Platform

Device code is split between generic abstractions and System/161/lamebus support:

- `src/kern/dev/generic/`: console, random, rtclock, beep.
- `src/kern/dev/lamebus/`: lhd disk, lser serial, ltimer, lrandom, emu, ltrace, bus attach logic.
- `src/kern/arch/sys161/`: System/161 platform glue.
- `src/kern/arch/mips/`: trap handling, syscall entry, context switching, TLB, CPU code, low-level assembly.

Network and screen-related devices are present in source/config but disabled or marked unsupported in default configs.

## Build Configuration

Kernel configuration is under `src/kern/conf/`.

- `DUMBVM` and `DUMBVM-OPT` are active-style teaching configs.
- `GENERIC` and `GENERIC-OPT` are broader configs.
- `conf.kern`, `config`, and architecture `conf.arch` files drive generated build directories.

Generated kernel build output is under `src/kern/compile/DUMBVM/`.
