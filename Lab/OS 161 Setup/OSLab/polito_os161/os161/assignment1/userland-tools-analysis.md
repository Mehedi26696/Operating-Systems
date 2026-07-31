# Userland, Tools, and Documentation Analysis

## Userland Layout

`src/userland/` contains headers, libc, command-line programs, system utilities, tests, and host compatibility code.

Major sections:

- `include/`: exported user C library and kernel ABI headers.
- `lib/libc/`: userland libc wrappers and routines.
- `lib/crt0/`: MIPS process startup assembly.
- `lib/hostcompat/`: compatibility support for host-built tools.
- `bin/`: standard small commands.
- `sbin/`: filesystem/admin utilities.
- `testbin/`: assignment and regression tests.

## libc

The libc implementation is intentionally small.

- `src/common/libc/string/`: shared `memcpy`, `memmove`, `memset`, `strlen`, `strcmp`, `strcpy`, `strcat`, `strchr`, `strrchr`, `strtok_r`, `bzero`.
- `src/common/libc/printf/`: shared printf formatting core and `snprintf`.
- `src/userland/lib/libc/stdio/`: thin user I/O helpers such as `printf`, `puts`, `putchar`, `getchar`.
- `src/userland/lib/libc/stdlib/`: `malloc`, `qsort`, `random`, `system`, `exit`, `abort`, `getenv`.
- `src/userland/lib/libc/unix/`: `errno`, `err`, `execvp`, `getcwd`, assertions.
- `src/userland/lib/libc/arch/mips/syscalls-mips.S`: MIPS syscall wrapper ABI glue.

The user ABI headers are mirrored into kernel/shared `kern` headers so userland can compile against syscall numbers, errno values, stat structures, and constants.

## Commands

`src/userland/bin/` includes compact Unix-like utilities:

- `cat`, `cp`, `false`, `ln`, `ls`, `mkdir`, `mv`, `pwd`, `rm`, `rmdir`, `sh`, `sync`, `tac`, `true`.

These programs are intended to exercise filesystem, process, and syscall support. Some commands will only become fully useful when the kernel implements open/read/write/close, fork/exec/wait, chdir/getcwd, and related syscalls.

## System Utilities

`src/userland/sbin/` contains:

- `halt`, `poweroff`, `reboot`;
- `mksfs`, `dumpsfs`, `sfsck`.

The SFS utilities operate on filesystem images and are also useful for debugging the kernel SFS implementation. The `sfsck` utility is structured into passes, inode/freemap/superblock modules, and compatibility helpers.

## Test Programs

`src/userland/testbin/` is extensive. It includes tests for:

- argument passing: `argtest`;
- process/syscall behavior: `forktest`, `forkbomb`, `bigfork`, `bigexec`, `multiexec`, `badcall`, `randcall`;
- VM/memory behavior: `sbrktest`, `parallelvm`, `malloctest`, `huge`, `bloat`;
- filesystem behavior: `filetest`, `bigfile`, `bigseek`, `dirtest`, `dirseek`, `dirconc`, `f_test`, `frack`, `poisondisk`, `sparsefile`, `rmtest`, `rmdirtest`;
- scheduler/thread interaction: `schedpong`, `farm`, `hog`;
- general utilities/math: `add`, `factorial`, `hash`, `matmult`, `palin`, `sort`, `triplesort`, `triplemat`, `tictac`, `tail`, `zero`.

Many tests deliberately accept `ENOSYS` for assignment syscalls that may not exist yet. That makes the suite useful across multiple OS/161 milestones.

## Common GCC Millicode

`src/common/gcc-millicode/` provides 64-bit arithmetic helper routines and support code such as `adddi3`, `divdi3`, `moddi3`, shifts, comparisons, and `qdivrem`. These satisfy compiler-emitted helper calls for the MIPS target.

## Build System

`src/mk/` contains reusable make fragments:

- program/library/host program rules;
- include installation;
- kernel compilation rules;
- config and dependency helpers.

Top-level `src/Makefile`, `src/configure`, and `src/defs.mk` tie the build together. `src/build/` is generated and should not be treated as canonical source.

## Testscripts

`src/testscripts/test.py` and `src/testscripts/runtest.py` provide Python test automation. Installed copies also appear under `root/testscripts/`.

## Manuals and Design Text

`src/man/` contains source documentation for bin/sbin/libc/syscalls/devices/testbin. Installed copies appear in `root/man/` and `src/build/install/man/`.

`src/design/` includes assignment/design text files:

- `assignments.txt`
- `shell.txt`
- `usermalloc.txt`

## Root Runtime Tree

`root/` is the installed System/161 root:

- `root/bin`, `root/sbin`, `root/testbin`: compiled MIPS user programs.
- `root/include`: installed headers.
- `root/lib`: installed `libc.a`, `libtest.a`, `crt0.o`.
- `root/man`: installed documentation.
- `root/kernel-DUMBVM`: built kernel.
- `root/LHD0.img`, `root/LHD1.img`: disk images.
- `root/sys161.conf`: simulator configuration.

The tree is runtime/deployment output and should normally be regenerated from `src`.

## Tools Tree

`tools/` contains the external toolchain and simulator stack:

- System/161 binaries: `sys161`, `disk161`, `hub161`, `stat161`, `trace161`.
- MIPS cross tools: GCC, binutils, GDB, linker scripts, libraries.
- Documentation and manpages for simulator/toolchain use.

This area is not OS/161 project logic; it is the environment used to build and run the project.
