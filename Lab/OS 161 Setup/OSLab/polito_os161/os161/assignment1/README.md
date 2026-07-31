# OS/161 Repository Analysis

This folder contains a codebase-level analysis of the OS/161 tree in this workspace.

The repository contains source files, build products, installed root filesystem artifacts, simulator/toolchain binaries, manuals, and disk images. The useful analysis target is the project source and configuration; generated objects, installed binaries, and disk images are covered by inventory and role rather than line-by-line reverse engineering.

## Analysis Files

- [source-inventory.md](source-inventory.md): repository layout, file categories, and what each area contains.
- [kernel-analysis.md](kernel-analysis.md): kernel boot, threads, synchronization, process, syscall, VM, VFS, filesystem, device, and architecture notes.
- [userland-tools-analysis.md](userland-tools-analysis.md): user programs, libc, tests, system utilities, scripts, manuals, and bundled tools.
- [implementation-gaps.md](implementation-gaps.md): unfinished assignment surfaces and risk areas found in the tree.

## High-Level Summary

This is an OS/161 educational operating system workspace with a MIPS/System/161 target. The active source is under `src/`; `root/` is the installed runtime tree used by the simulator; `tools/` contains the cross toolchain and System/161 utilities; `src/build/` and `src/kern/compile/DUMBVM/` are generated build output.

The kernel is configured around `DUMBVM`, has the stock boot/menu/test structure, and includes many complete base subsystems: thread scheduling, semaphores, the VFS layer, SFS/semfs, lamebus devices, common libc routines, and userland programs. Several assignment-facing pieces are intentionally incomplete, especially locks/CVs, general system calls, process lifecycle, and the non-DUMBVM address-space implementation.
