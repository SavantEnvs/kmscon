/*
 * asciicast v2/v3 player (src/misc/asciinema.c, kmscon --asciicast <file>):
 * header + event-line parser and its JSON string decoder. The input is the cast
 * file; it is loaded with kmscon_asciinema_new and played to the end by
 * stepping kmscon_asciinema_advance (what the eloop timer does), with the
 * decoded output fed to a sink that checks it is a bounded byte string.
 */
#include <stdbool.h>
#include <stdint.h>
#include "asciinema.h"
#include "shl/eloop.h"
#include "fuzz_common.h"

static struct ev_eloop *eloop;
static size_t total;

static void sink(const char *u8, size_t len, void *data)
{
	(void)data;
	volatile char c = 0;
	if (len)
		c = u8[0] ^ u8[len - 1];
	(void)c;
	total += len;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc; (void)argv;
	if (ev_eloop_new(&eloop))
		abort();
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct kmscon_asciinema *player = NULL;
	const char *path = fuzz_write_file(".cast", data, size);
	uint64_t delay;
	total = 0;

	if (kmscon_asciinema_new(&player, eloop, path, false, sink, NULL))
		return 0;
	if (kmscon_asciinema_start(player)) {
		/* each event is advanced once; a cast has at most size events */
		for (size_t n = 0; n <= size + 1; n++) {
			if (!kmscon_asciinema_advance(player, &delay))
				break;
			if (n == 1) {
				kmscon_asciinema_pause(player);
				kmscon_asciinema_resume(player);
			}
		}
		kmscon_asciinema_stop(player);
	}
	kmscon_asciinema_free(player);
	return 0;
}
