# Basic Console Syscalls Implementation

This note documents the implementation added to stop the OS/161 shell from repeatedly printing:

```text
Unknown syscall 55
Unknown syscall 50
```

Those numbers come from `src/kern/include/kern/syscall.h`:

```c
#define SYS_read         50
#define SYS_write        55
```

The shell was starting, but it could not read from stdin or write to stdout because the kernel syscall dispatcher only handled `reboot` and `__time`.

## File 1: `src/kern/include/syscall.h`

### Where I added code

I added these declarations in the section named:

```c
/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */
```

They were added immediately after the existing `sys_reboot` and `sys___time` prototypes.

### Code added

```c
int sys_read(int fd, userptr_t buf, size_t buflen, int32_t *retval);
int sys_write(int fd, const_userptr_t buf, size_t nbytes, int32_t *retval);
__DEAD void sys__exit(int code);
```

### Why I added it

`syscall.c` calls kernel functions such as `sys_read`, `sys_write`, and `sys__exit`. The header needs prototypes so those functions are visible to the syscall dispatcher and compile cleanly.

The `retval` pointer is used for `read` and `write` because syscall functions need to return two things:

- the kernel error code as the C function return value;
- the user-visible return value, such as the number of bytes read or written.

`sys__exit` is marked `__DEAD` because it should not return.

## File 2: `src/kern/arch/mips/syscall/syscall.c`

### Where I added code

I added cases inside the existing syscall dispatcher:

```c
switch (callno) {
        ...
}
```

The new cases are placed near the existing `SYS_reboot` and `SYS___time` cases, before the `default` case that prints `Unknown syscall`.

### Code added

```c
case SYS__exit:
        sys__exit(tf->tf_a0);
        panic("sys__exit returned\n");
        break;

case SYS_read:
        err = sys_read((int)tf->tf_a0, (userptr_t)tf->tf_a1,
                       (size_t)tf->tf_a2, &retval);
        break;

case SYS_write:
        err = sys_write((int)tf->tf_a0, (const_userptr_t)tf->tf_a1,
                        (size_t)tf->tf_a2, &retval);
        break;
```

### Why I added it

Before this change, syscall numbers `50` and `55` went to the `default` case:

```c
kprintf("Unknown syscall %d\n", callno);
err = ENOSYS;
```

The new cases connect userland calls to kernel implementations:

- `SYS_read` number `50` goes to `sys_read`;
- `SYS_write` number `55` goes to `sys_write`;
- `SYS__exit` number `3` goes to `sys__exit`.

The MIPS syscall ABI passes the first three arguments in registers, stored in the trapframe as:

- `tf_a0`: file descriptor or exit code;
- `tf_a1`: user buffer pointer;
- `tf_a2`: byte count.

## File 3: `src/kern/syscall/console_syscalls.c`

### Where I added code

This is a new file under the kernel syscall directory:

```text
src/kern/syscall/console_syscalls.c
```

It contains minimal implementations of `read`, `write`, and `_exit`.

### Code added

```c
/*
 * Minimal console-backed system calls.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <thread.h>
#include <copyinout.h>
#include <syscall.h>

#define CONSOLE_IO_CHUNK 128
```

These includes provide errno values, standard fd constants, `putch`, `getch`, `thread_exit`, and safe user/kernel memory copying.

### `sys_write`

```c
int
sys_write(int fd, const_userptr_t buf, size_t nbytes, int32_t *retval)
{
        char kbuf[CONSOLE_IO_CHUNK];
        size_t done, chunk, i;
        int result;

        if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
                return EBADF;
        }

        done = 0;
        while (done < nbytes) {
                chunk = nbytes - done;
                if (chunk > sizeof(kbuf)) {
                        chunk = sizeof(kbuf);
                }

                result = copyin((const_userptr_t)((vaddr_t)buf + done),
                                kbuf, chunk);
                if (result) {
                        return result;
                }

                for (i = 0; i < chunk; i++) {
                        if (kbuf[i] == '\n') {
                                putch('\r');
                        }
                        putch(kbuf[i]);
                }
                done += chunk;
        }

        *retval = (int32_t)done;
        return 0;
}
```

Why this was added:

- Userland `printf` calls `write(STDOUT_FILENO, ...)`.
- `STDOUT_FILENO` is fd `1`; `STDERR_FILENO` is fd `2`.
- `copyin` safely copies bytes from user memory into a kernel buffer.
- `putch` writes each byte to the OS/161 console.
- Returning `done` gives userland the normal `write` return value.

This is console-only. It does not implement real file descriptors yet.

### `sys_read`

```c
int
sys_read(int fd, userptr_t buf, size_t buflen, int32_t *retval)
{
        size_t done;
        char ch;
        int result;

        if (fd != STDIN_FILENO) {
                return EBADF;
        }

        done = 0;
        while (done < buflen) {
                ch = getch();
                if (ch == '\r') {
                        ch = '\n';
                }

                result = copyout(&ch, (userptr_t)((vaddr_t)buf + done), 1);
                if (result) {
                        return result;
                }

                if (ch == '\n') {
                        putch('\r');
                }
                putch(ch);

                done++;
                if (ch == '\n') {
                        break;
                }
        }

        *retval = (int32_t)done;
        return 0;
}
```

Why this was added:

- The shell calls `read(STDIN_FILENO, ...)` to get keyboard input.
- `STDIN_FILENO` is fd `0`.
- `getch` reads one character from the kernel console.
- `copyout` safely writes that character into the user buffer.
- The loop stops on newline so shell line input can proceed naturally.

This is also console-only. It does not read from arbitrary files.

### `sys__exit`

```c
void
sys__exit(int code)
{
        (void)code;
        thread_exit();
}
```

Why this was added:

- When a user program returns from `main`, userland `crt0` calls `exit`, which eventually calls `_exit`.
- Without `_exit`, user programs cannot terminate normally.
- This minimal version terminates the current thread.

This is not a full process lifecycle implementation. It does not preserve exit status for `waitpid`, because `waitpid` and PID tracking are not implemented yet.

## File 4: `src/kern/conf/conf.kern`

### Where I added code

I added the new source file in the existing system call layer section:

```text
#
# System call layer
#
```

### Code added

```text
file      syscall/console_syscalls.c
```

### Why I added it

OS/161 kernel source files must be registered in `conf.kern`, otherwise they are not compiled into the kernel after running config.

## File 5: `src/kern/compile/DUMBVM/files.mk`

### Where I added code

I added the generated-build equivalent next to the other syscall source files.

### Code added

```text
SRCS+=$(KTOP)/syscall/console_syscalls.c
```

### Why I added it

`src/kern/compile/DUMBVM/files.mk` is generated by the OS/161 config system, but this workspace already has an existing DUMBVM compile directory. Updating it lets the current DUMBVM build directory include the new file immediately.

In a clean workflow, you should still regenerate it with:

```sh
cd src/kern/conf
./config DUMBVM
```

## Rebuild Steps

Use your OS/161 build shell, not plain Windows PowerShell, because this environment does not currently have `bmake`, `make`, or `gmake` available.

```sh
cd src/kern/conf
./config DUMBVM
cd ../compile/DUMBVM
bmake depend
bmake
bmake install
```

Then run from `root`:

```sh
cd ../../../root
../tools/bin/sys161 kernel-DUMBVM
```

At the kernel prompt:

```text
s
```

The repeated messages for syscall `50` and `55` should stop.

## What This Fixes

This enables minimal console I/O:

- `read(0, buf, len)` reads from the OS/161 console;
- `write(1, buf, len)` writes to the OS/161 console;
- `write(2, buf, len)` writes to the OS/161 console;
- `_exit(code)` terminates the current user thread.

## What This Does Not Yet Fix

This does not implement:

- `open`, `close`, `lseek`, or a file descriptor table;
- `fork`, `execv`, `waitpid`, or `getpid`;
- full process cleanup or exit status tracking;
- argument passing for `p /testbin/add 2 3`.

So `/bin/sh` may progress further, but it can still hit other unknown syscalls. `add.c` still needs argument passing before it can receive `2` and `3`.
## Troubleshooting: `*: Unrecognized source file type`

If `./config DUMBVM` prints:

```text
*: Unrecognized source file type
```

then the config parser is probably seeing Windows CRLF line endings in `src/kern/conf/conf.kern`. OS/161's `config` script validates source names with awk patterns like:

```awk
/\.c$/ { next; }
```

If a line actually ends with `.c\r`, that pattern does not match, so the script reports the first field, which is `*` in its generated file list.

I converted the touched config files back to Unix LF endings:

```text
src/kern/conf/conf.kern
src/kern/compile/DUMBVM/files.mk
```

After that, rerun:

```sh
cd src/kern/conf
./config DUMBVM
```
## Install Path Issue After Successful Build

The kernel build completed successfully:

```text
*** This is DUMBVM build #2 ***
mips-harvard-os161-size kernel
```

The first install attempt failed because `OSTREE` in `src/defs.mk` points to:

```make
OSTREE=/home/os161user/os161/root
```

but that parent directory did not exist yet:

```text
mkdir: cannot create directory '/home/os161user/os161/root': No such file or directory
```

### What Happened Next

This command created the parent directory:

```sh
mkdir -p /home/os161user/os161
```

Then this command succeeded:

```sh
bmake install
```

It installed the kernel into:

```text
/home/os161user/os161/root/kernel-DUMBVM
```

Then this command was run:

```sh
ln -s "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root" /home/os161user/os161/root
```

Because `/home/os161user/os161/root` already existed as a real directory, this did not replace it with a symlink. It likely created a symlink inside that directory instead.

That means there are now two possible root trees:

```text
/home/os161user/os161/root
/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root
```

The new kernel from `bmake install` is in the first one unless manually copied.

## Correct Immediate Fix

Copy the built kernel from the install directory to the workspace root used by your simulator:

```sh
cp /home/os161user/os161/root/kernel-DUMBVM "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root/kernel-DUMBVM"
```

Then boot from the workspace root:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
../tools/bin/sys161 kernel-DUMBVM
```

This is the safest immediate path because it does not delete anything.

## Optional Permanent Fix

If you want future `bmake install` runs to install directly into the workspace root, replace the real `/home/os161user/os161/root` directory with a symlink.

Only do this if you are okay deleting the separate install directory at `/home/os161user/os161/root`:

```sh
rm -rf /home/os161user/os161/root
ln -s "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root" /home/os161user/os161/root
```

After that, future installs should copy directly into the workspace root:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/src/kern/compile/DUMBVM"
bmake install
```

## Alternative Fix: Change `OSTREE`

Another option is editing `src/defs.mk`:

```make
OSTREE=/home/os161user/os161/root
```

But this project path contains spaces:

```text
/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root
```

OS/161 makefiles often use `$(OSTREE)` without quotes, so using the full spaced path directly in `OSTREE` is risky. A no-space symlink path such as `/home/os161user/os161/root` is cleaner.

## Verification After Install

After copying or fixing the symlink, boot OS/161 and start the shell:

```text
s
```

Expected improvement:

```text
Unknown syscall 50
Unknown syscall 55
```

should no longer repeat, because `read`, `write`, and `_exit` are now implemented minimally.

Other unknown syscalls may still appear because the shell can also need process and filesystem syscalls that are not implemented yet.
## Backspace/Delete Handling

After the first `read` implementation, typing mistakes in the OS/161 shell showed visible delete characters like:

```text
/tteestbtin/nadd^^^^^^^^
```

or as the raw DEL glyph:

```text
/tteestbtin/nadd
```

The reason is that `sys_read` echoed every input byte directly with `putch(ch)`. In System/161, Backspace often arrives as ASCII `127` (`DEL`), not as `\b`, and the minimal read implementation was copying that byte into the shell input buffer.

### File Changed

```text
src/kern/syscall/console_syscalls.c
```

### Where I Added Code

Inside `sys_read`, immediately after converting carriage return to newline and before `copyout()` writes the byte to the user buffer.

### Code Added

```c
if (ch == '\b' || ch == 127) {
        if (done > 0) {
                done--;
                putch('\b');
                putch(' ');
                putch('\b');
        }
        continue;
}
```

### Why I Added It

This makes Backspace/Delete behave like normal terminal editing:

- if there is at least one buffered character, `done--` removes it from the input line;
- `putch('\b')` moves the cursor back;
- `putch(' ')` visually erases the old character;
- `putch('\b')` moves the cursor back again;
- `continue` prevents the delete byte from being copied into user memory.

After rebuilding and installing the kernel, typing mistakes should no longer leave visible `\177`/`DEL` characters in the OS/161 shell.

### Rebuild After This Change

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/src/kern/compile/DUMBVM"
bmake depend
bmake
bmake install
```

Then boot again:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
../tools/bin/sys161 kernel-DUMBVM
```
## Correction: Remove Kernel Echo From `sys_read`

The first Backspace/Delete fix still caused echo problems because the userland shell already performs interactive line editing.

In `src/userland/bin/sh/sh.c`, the shell's `getcmd()` function does this:

```c
ch = getchar();
if ((ch == '\b' || ch == 127) && pos > 0) {
        putchar('\b');
        putchar(' ');
        putchar('\b');
        pos--;
}
else if (ch == '\r' || ch == '\n') {
        putchar('\r');
        putchar('\n');
        done = 1;
}
else if (ch >= 32 && ch < 127 && pos < len-1) {
        buf[pos++] = ch;
        putchar(ch);
}
```

So the kernel must not also echo characters or handle Backspace as line editing. If it does, every typed character or erase action can appear twice or behave strangely.

### File Changed

```text
src/kern/syscall/console_syscalls.c
```

### What Was Removed

The kernel-side echo/editing block was removed from `sys_read`:

```c
if (ch == '\b' || ch == 127) {
        if (done > 0) {
                done--;
                putch('\b');
                putch(' ');
                putch('\b');
        }
        continue;
}
...
if (ch == '\n') {
        putch('\r');
}
putch(ch);
```

### Current Correct `sys_read` Behavior

Now `sys_read` only reads a byte from the console and copies it to user memory:

```c
ch = getch();
if (ch == '\r') {
        ch = '\n';
}

result = copyout(&ch, (userptr_t)((vaddr_t)buf + done), 1);
if (result) {
        return result;
}

done++;
if (ch == '\n') {
        break;
}
```

### Why This Is Better

This matches the layering:

- kernel `read`: supplies input bytes;
- userland shell `getcmd`: decides how to echo printable characters, newline, Backspace, and Delete.

After rebuilding, typing in `OS/161$` should look normal: no duplicate echo and no raw `DEL` characters when Backspace is used.