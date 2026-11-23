/*
 * /etc/issue rendering (src/misc/issue.c + issue_network.c): kmscon reads the
 * issue file(s) and expands agetty-style backslash escapes (\n, \l, \S{...},
 * \4{iface}, \e{color}, ...) before showing the login prompt. The input is the
 * issue file, passed as the search path exactly like ISSUE_DEFAULT_PATH entries.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "issue.h"
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const char *path = fuzz_write_file("", data, size);
	char tty[] = "tty1";
	size_t len = 0;
	char *buf = kmscon_issue_get_buffer(path, tty, &len);
	if (buf) {
		volatile char c = len ? buf[len - 1] : 0;
		(void)c;
		free(buf);
	}
	return 0;
}
