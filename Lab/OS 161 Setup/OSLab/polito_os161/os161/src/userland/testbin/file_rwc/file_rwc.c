/*
 * file_rwc - Assignment 2 test program.
 *
 * Demonstrates file operations required by the assignment:
 *
 *   - write    : create a file and write data to it
 *   - read     : open it again and read it back
 *   - append   : open with O_APPEND and append additional data
 *   - copy     : copy the contents from one file to another using
 *                a userspace read/write loop
 *
 * Usage:
 *
 *   /testbin/file_rwc srcfile dstfile
 *
 * Steps performed:
 *
 *   1. Write the contents of "hello\n" into "srcfile".
 *   2. Append "world\n" by reopening with O_APPEND.
 *   3. Copy the file to "dstfile" by reading src and writing dst.
 *   4. Read back dstfile into a buffer and print it so the user can
 *      eyeball the result.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#define BUFSZ 256

static const char part1[] = "hello\n";
static const char part2[] = "world\n";

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

static int
read_n(int fd, char *buf, size_t n)
{
	size_t done = 0;
	while (done < n) {
		ssize_t r = read(fd, buf + done, n - done);
		if (r == 0) {
			break;  /* EOF */
		}
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		done += (size_t)r;
	}
	return (int)done;
}

static int
copy_file(const char *src, const char *dst)
{
	int sfd, dfd;
	char buf[BUFSZ];
	ssize_t r;
	int iter = 0;

	printf("[copy_file] step A: open src=%s O_RDONLY\n", src);
	sfd = open(src, O_RDONLY);
	printf("[copy_file] step A done: sfd=%d\n", sfd);
	if (sfd < 0) {
		printf("copy: open %s for read failed: %s\n", src,
		       strerror(errno));
		return -1;
	}

	printf("[copy_file] step B: open dst=%s O_WRONLY|O_CREAT|O_TRUNC\n", dst);
	dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	printf("[copy_file] step B done: dfd=%d\n", dfd);
	if (dfd < 0) {
		printf("copy: open %s for write failed: %s\n", dst,
		       strerror(errno));
		close(sfd);
		return -1;
	}

	printf("[copy_file] step C: entering read loop\n");
	for (;;) {
		iter++;
		printf("[copy_file] step C.%d: read(sfd=%d, buf, %zu)\n",
		       iter, sfd, sizeof(buf));
		r = read(sfd, buf, sizeof(buf));
		printf("[copy_file] step C.%d: read returned r=%zd\n", iter, r);
		if (r == 0) {
			printf("[copy_file] EOF reached after %d iterations\n", iter);
			break;  /* EOF */
		}
		if (r < 0) {
			if (errno == EINTR) {
				printf("[copy_file] EINTR, continuing\n");
				continue;
			}
			printf("copy: read failed: %s\n", strerror(errno));
			close(sfd);
			close(dfd);
			return -1;
		}
		printf("[copy_file] step C.%d: write_all(dfd=%d, %zd)\n",
		       iter, dfd, r);
		if (write_all(dfd, buf, (size_t)r) < 0) {
			printf("copy: write failed: %s\n", strerror(errno));
			close(sfd);
			close(dfd);
			return -1;
		}
		printf("[copy_file] step C.%d: write_all returned\n", iter);
	}

	printf("[copy_file] step D: closing fds\n");
	close(sfd);
	close(dfd);
	printf("[copy_file] step E: done\n");
	return 0;
}

int
main(int argc, char **argv)
{
	const char *src, *dst;
	int fd;
	char buf[BUFSZ];
	int n, total;
	struct stat st;

	if (argc != 3) {
		printf("usage: file_rwc srcfile dstfile\n");
		return 1;
	}
	src = argv[1];
	dst = argv[2];

	/* Step 1: write "hello\n" to src */
	fd = open(src, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("open src for write failed: %s\n", strerror(errno));
		return 1;
	}
	printf("[write] writing %zu bytes to %s\n", sizeof(part1) - 1, src);
	if (write_all(fd, part1, sizeof(part1) - 1) < 0) {
		printf("write failed: %s\n", strerror(errno));
		return 1;
	}
	close(fd);

	/* Step 2: append "world\n" using O_APPEND */
	fd = open(src, O_WRONLY | O_APPEND);
	if (fd < 0) {
		printf("open src for append failed: %s\n", strerror(errno));
		return 1;
	}
	printf("[append] appending %zu bytes to %s\n",
	       sizeof(part2) - 1, src);
	if (write_all(fd, part2, sizeof(part2) - 1) < 0) {
		printf("append failed: %s\n", strerror(errno));
		return 1;
	}
	close(fd);

	/* Confirm the file size with fstat. */
	fd = open(src, O_RDONLY);
	if (fd < 0) {
		printf("reopen src failed: %s\n", strerror(errno));
		return 1;
	}
	if (fstat(fd, &st) < 0) {
		printf("fstat failed: %s\n", strerror(errno));
		return 1;
	}
	printf("[stat] %s size = %ld bytes (st_mode=0%llo, nlink=%u)\n",
	       src, (long)st.st_size,
	       (unsigned long long)st.st_mode, (unsigned)st.st_nlink);
	close(fd);

	/* Step 3: copy src -> dst */
	printf("[copy] %s -> %s\n", src, dst);
	if (copy_file(src, dst) < 0) {
		return 1;
	}

	/* Step 4: read back dst and print. */
	fd = open(dst, O_RDONLY);
	if (fd < 0) {
		printf("reopen dst failed: %s\n", strerror(errno));
		return 1;
	}
	total = 0;
	for (;;) {
		n = read_n(fd, buf, sizeof(buf) - 1);
		if (n < 0) {
			printf("read failed: %s\n", strerror(errno));
			return 1;
		}
		if (n == 0) {
			break;
		}
		buf[n] = '\0';
		printf("[read] %s", buf);
		total += n;
	}
	close(fd);
	printf("\n[done] read back %d bytes total\n", total);

	return 0;
}

