/*
 * libtsm (Rust, src/tsm) through its C API tsm.h: the VT parser/screen that
 * consumes every byte the child process writes to the pty.
 *
 * Input: 3 header bytes (cols, rows, scrollback), then a sequence of ops.
 * Most ops feed the next chunk of bytes to tsm_vte_input(); others resize,
 * scroll, select/copy, paste, and send keyboard/mouse events, as kmscon's
 * terminal.c does between pty reads. After each op the screen is rendered and
 * the documented invariant "cursor position is always inside the screen"
 * (tsm.h) is checked.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tsm.h"

static void on_write(void *data, const char *buf, size_t len) { (void)data; (void)buf; (void)len; }
static void on_osc(void *data, const char *str, size_t len)
{
	(void)data;
	if (strlen(str) != len)
		__builtin_trap();
}
static void on_bell(void *data) { (void)data; }
static void on_leds(void *data, unsigned int l) { (void)data; (void)l; }
static void on_mouse(void *data, bool e, bool p) { (void)data; (void)e; (void)p; }

static const struct tsm_callbacks cbs = {
	.write = on_write, .osc = on_osc, .bell = on_bell, .leds = on_leds, .mouse_mode = on_mouse,
};

static void check(struct tsm_vte *vte)
{
	unsigned int w = tsm_vte_get_width(vte), h = tsm_vte_get_height(vte), x, y;
	const struct tsm_cell *cells = tsm_vte_render(vte);
	volatile uint32_t sum = 0;
	if (cells)
		for (unsigned int i = 0; i < w * h; i++)
			sum += cells[i].ch;
	tsm_vte_get_cursor(vte, &x, &y);
	if (w && h && (x >= w || y >= h))
		__builtin_trap(); /* tsm.h: cursor position, always inside the screen */
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct tsm_vte *vte;
	if (size < 3)
		return -1;
	if (tsm_vte_new(&vte, &cbs, NULL) != 0)
		return 0;
	tsm_vte_resize(vte, 1 + data[0] % 160, 1 + data[1] % 60);
	tsm_vte_set_max_scrollback(vte, data[2]);
	size_t i = 3;
	while (i < size) {
		uint8_t op = data[i++];
		size_t avail = size - i;
		switch (op >> 5) {
		case 0: case 1: case 2: case 3: {
			/* VT bytes: chunk of length op+1 (capped by what's left) */
			size_t n = (size_t)(op & 0x7f) + 1;
			if (n > avail)
				n = avail;
			tsm_vte_input(vte, (const char *)data + i, n);
			i += n;
			break;
		}
		case 4:
			if (avail < 2)
				goto out;
			tsm_vte_resize(vte, 1 + data[i] % 160, 1 + data[i + 1] % 60);
			i += 2;
			break;
		case 5: {
			if (avail < 4)
				goto out;
			char *copy = NULL;
			tsm_vte_selection_start(vte, data[i], data[i + 1]);
			if (op & 1)
				tsm_vte_selection_word(vte, data[i + 2], data[i + 3]);
			else
				tsm_vte_selection_target(vte, data[i + 2], data[i + 3]);
			int r = tsm_vte_selection_copy(vte, &copy);
			if (r >= 0 && copy && strlen(copy) != (size_t)r)
				__builtin_trap(); /* tsm.h: NUL-terminated, returns its length */
			free(copy);
			if (op & 2)
				tsm_vte_selection_reset(vte);
			i += 4;
			break;
		}
		case 6:
			if (avail < 1)
				goto out;
			switch (op & 7) {
			case 0: tsm_vte_sb_up(vte, data[i]); break;
			case 1: tsm_vte_sb_down(vte, data[i]); break;
			case 2: tsm_vte_sb_page_up(vte, data[i] % 4); break;
			case 3: tsm_vte_sb_page_down(vte, data[i] % 4); break;
			case 4: tsm_vte_sb_reset(vte); break;
			case 5: tsm_vte_hard_reset(vte); break;
			default: {
				size_t n = data[i] < avail - 1 ? data[i] : avail - 1;
				tsm_vte_paste(vte, (const char *)data + i + 1, n);
				i += n;
			}
			}
			i += 1;
			break;
		default:
			if (avail < 6)
				goto out;
			if (op & 1) {
				uint32_t ks = (uint32_t)data[i] << 8 | data[i + 1];
				tsm_vte_handle_keyboard(vte, ks | (op & 2 ? 0xff00 : 0), data[i + 2],
							data[i + 3], (uint32_t)data[i + 4] << 8 | data[i + 5]);
			} else {
				tsm_vte_handle_mouse(vte, data[i], data[i + 1], data[i] * 8u,
						     data[i + 1] * 16u, data[i + 2], data[i + 3] & 3,
						     data[i + 4]);
			}
			i += 6;
			break;
		}
		check(vte);
	}
out:
	check(vte);
	tsm_vte_free(vte);
	return 0;
}
