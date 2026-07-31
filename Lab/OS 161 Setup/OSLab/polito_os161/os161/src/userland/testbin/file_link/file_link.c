/*
 * file_link - Assignment 2 test program for symlink/readlink.
 *
 *   /testbin/file_link <content> <linkname>
 *
 * Creates a symlink pointing at <content>, then uses readlink() to
 * read it back and prints the result.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int
main(int argc, char **argv)
{
	const char *contents, *name;
	char buf[256];
	ssize_t n;

	if (argc != 3) {
		printf("usage: file_link <contents> <linkname>\n");
		return 1;
	}
	contents = argv[1];
	name = argv[2];

	printf("[symlink] creating symlink %s -> %s\n", name, contents);
	if (symlink(contents, name) < 0) {
		printf("symlink failed: %s\n", strerror(errno));
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	n = readlink(name, buf, sizeof(buf) - 1);
	if (n < 0) {
		printf("readlink failed: %s\n", strerror(errno));
		return 1;
	}
	buf[n] = '\0';
	printf("[readlink] %s = %s (%d bytes)\n", name, buf, (int)n);

	return 0;
}
