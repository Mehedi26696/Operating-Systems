/*
 * Minimal console-backed system calls.
 *
 * Console-backed syscalls cover stdin/stdout/stderr. These were originally
 * the only implementations of sys_read/sys_write; they are still here as
 * the backing helpers for those fds after Assignment 2 introduces a real
 * fd table for regular files.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <copyinout.h>
#include <syscall.h>

#define CONSOLE_IO_CHUNK 128

/*
 * Internal helper: write to the kernel console.
 *
 * Used for sys_write(1/2, ...) and any other internal printing path
 * that wants to surface user buffer contents via the console.
 */
int
console_write(const_userptr_t buf, size_t nbytes, int32_t *retval)
{
	char kbuf[CONSOLE_IO_CHUNK];
	size_t done, chunk, i;
	int result;

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

/*
 * Internal helper: read a line from the kernel console.
 */
int
console_read(userptr_t buf, size_t buflen, int32_t *retval)
{
	size_t done;
	char ch;
	int result;

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

		done++;
		if (ch == '\n') {
			break;
		}
	}

	*retval = (int32_t)done;
	return 0;
}

void
sys__exit(int code)
{
	proc_exit(code);
	thread_exit();
}
