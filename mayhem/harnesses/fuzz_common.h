/* Shared helper: stage the fuzz input as a file for APIs that take a path. */
#ifndef FUZZ_COMMON_H
#define FUZZ_COMMON_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* One scratch file per process under $TMPDIR (fallback /tmp), truncated and
 * rewritten for every input, so each run only sees the current input. */
static const char *fuzz_write_file(const char *suffix, const uint8_t *data, size_t size)
{
	static char path[4096];
	static int fd = -1;
	if (fd < 0) {
		const char *dir = getenv("TMPDIR");
		if (!dir || !*dir)
			dir = "/tmp";
		snprintf(path, sizeof(path), "%s/kmscon-fuzz-XXXXXX%s", dir, suffix);
		fd = mkstemps(path, (int)strlen(suffix));
		if (fd < 0) {
			perror("mkstemps");
			abort();
		}
	}
	if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) != 0)
		abort();
	size_t off = 0;
	while (off < size) {
		ssize_t w = write(fd, data + off, size - off);
		if (w <= 0)
			abort();
		off += (size_t)w;
	}
	return path;
}
#endif
