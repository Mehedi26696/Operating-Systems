/*
 * fileoperations - Assignment 2 file-operations test program.
 *
 * A small multi-mode file utility that exercises the core file syscalls
 * on real files (e.g. /testbin/sample.txt). Modes:
 *
 *   cat    file [file ...]      read each file and print it (default mode)
 *   read   file [file ...]      alias for cat
 *   write  file  text...        create/truncate file, write "text\n"
 *   append file  text...        open O_APPEND, append "text\n"
 *   copy   src dst              copy src -> dst byte for byte
 *
 * If the first argument is not a known mode, every argument is treated
 * as a file to cat (backward compatible with the original fileoperations).
 *
 * Examples (from the OS/161 menu, argument passing must be enabled):
 *
 *   p /testbin/fileoperations cat    /testbin/sample.txt
 *   p /testbin/fileoperations copy   /testbin/sample.txt /mycopy
 *   p /testbin/fileoperations cat    /mycopy
 *   p /testbin/fileoperations write  /note.txt hello there
 *   p /testbin/fileoperations append /note.txt second line
 *   p /testbin/fileoperations cat    /note.txt
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define BUFSZ 256

/* Write exactly len bytes, retrying short/interrupted writes. */
static int
write_all(int fd, const char *data, size_t len)
{
	size_t done = 0;
	while (done < len) {
		ssize_t n = write(fd, data + done, len - done);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		done += (size_t)n;
	}
	return 0;
}

/* Copy the whole contents of one open fd to stdout. */
static int
cat_fd(int fd, const char *name)
{
	char buf[BUFSZ];
	ssize_t r;

	for (;;) {
		r = read(fd, buf, sizeof(buf));
		if (r == 0) {
			break;  /* EOF */
		}
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			printf("fileoperations: read %s failed: %s\n", name,
			       strerror(errno));
			return -1;
		}
		if (write_all(STDOUT_FILENO, buf, (size_t)r) < 0) {
			printf("fileoperations: write failed: %s\n", strerror(errno));
			return -1;
		}
	}
	return 0;
}

/* cat/read: print each named file to stdout. */
static int
do_cat(int argc, char **argv)
{
	int i, fd, status = 0;

	if (argc < 1) {
		printf("usage: fileoperations cat file [file ...]\n");
		return 1;
	}
	for (i = 0; i < argc; i++) {
		fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			printf("fileoperations: open %s failed: %s\n", argv[i],
			       strerror(errno));
			status = 1;
			continue;
		}
		if (cat_fd(fd, argv[i]) < 0) {
			status = 1;
		}
		close(fd);
	}
	return status;
}

/*
 * Join argv[0..argc-1] into buf separated by single spaces, followed by
 * a trailing newline. Used to reassemble menu-tokenized text for the
 * write/append modes. Returns the number of bytes placed in buf.
 */
static size_t
join_words(char *buf, size_t cap, int argc, char **argv)
{
	size_t len = 0;
	int i;

	for (i = 0; i < argc && len < cap - 1; i++) {
		if (i > 0 && len < cap - 1) {
			buf[len++] = ' ';
		}
		size_t wlen = strlen(argv[i]);
		if (wlen > cap - 1 - len) {
			wlen = cap - 1 - len;
		}
		memcpy(buf + len, argv[i], wlen);
		len += wlen;
	}
	if (len < cap) {
		buf[len++] = '\n';
	}
	return len;
}

/* write: create/truncate file and write the given text + newline. */
static int
do_write(int argc, char **argv, int append)
{
	const char *path;
	char text[BUFSZ];
	size_t len;
	int fd, flags;

	if (argc < 2) {
		printf("usage: fileoperations %s file text...\n",
		       append ? "append" : "write");
		return 1;
	}
	path = argv[0];
	len = join_words(text, sizeof(text), argc - 1, argv + 1);

	if (append) {
		flags = O_WRONLY | O_APPEND;   /* file must already exist */
	} else {
		flags = O_WRONLY | O_CREAT | O_TRUNC;
	}

	fd = open(path, flags, 0644);
	if (fd < 0) {
		printf("fileoperations: open %s failed: %s\n", path,
		       strerror(errno));
		return 1;
	}
	if (write_all(fd, text, len) < 0) {
		printf("fileoperations: write %s failed: %s\n", path,
		       strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("fileoperations: %s %zu bytes to %s\n",
	       append ? "appended" : "wrote", len, path);
	return 0;
}

/* copy: read src and write it to dst (create/truncate). */
static int
do_copy(int argc, char **argv)
{
	const char *src, *dst;
	char buf[BUFSZ];
	ssize_t r;
	int sfd, dfd;
	size_t total = 0;

	if (argc != 2) {
		printf("usage: fileoperations copy src dst\n");
		return 1;
	}
	src = argv[0];
	dst = argv[1];

	sfd = open(src, O_RDONLY);
	if (sfd < 0) {
		printf("fileoperations: open %s failed: %s\n", src, strerror(errno));
		return 1;
	}
	dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (dfd < 0) {
		printf("fileoperations: open %s failed: %s\n", dst, strerror(errno));
		close(sfd);
		return 1;
	}

	for (;;) {
		r = read(sfd, buf, sizeof(buf));
		if (r == 0) {
			break;  /* EOF */
		}
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			printf("fileoperations: read %s failed: %s\n", src,
			       strerror(errno));
			close(sfd);
			close(dfd);
			return 1;
		}
		if (write_all(dfd, buf, (size_t)r) < 0) {
			printf("fileoperations: write %s failed: %s\n", dst,
			       strerror(errno));
			close(sfd);
			close(dfd);
			return 1;
		}
		total += (size_t)r;
	}
	close(sfd);
	close(dfd);
	printf("fileoperations: copied %zu bytes %s -> %s\n", total, src, dst);
	return 0;
}

int
main(int argc, char **argv)
{
	const char *mode;

	if (argc < 2) {
		printf("usage: fileoperations <mode> args...\n");
		printf("  modes: cat|read file...   write file text...\n");
		printf("         append file text...   copy src dst\n");
		return 1;
	}

	mode = argv[1];

	if (!strcmp(mode, "cat") || !strcmp(mode, "read")) {
		return do_cat(argc - 2, argv + 2);
	}
	if (!strcmp(mode, "write")) {
		return do_write(argc - 2, argv + 2, 0 /* truncate */);
	}
	if (!strcmp(mode, "append")) {
		return do_write(argc - 2, argv + 2, 1 /* append */);
	}
	if (!strcmp(mode, "copy")) {
		return do_copy(argc - 2, argv + 2);
	}

	/* Unknown mode: treat every argument as a file to cat. */
	return do_cat(argc - 1, argv + 1);
}
