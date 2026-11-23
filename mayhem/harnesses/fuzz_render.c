/*
 * Software rendering of a fuzzed terminal: the VT stream (libtsm) drawn by the
 * bbulk renderer (src/render/render.c + bbulk.c) with the built-in 8x16 font
 * and block elements (src/font/font.c, font_8x16.c, blocks.c) — what kmscon's
 * terminal.c does on every redraw (render_prepare/render_draw/render_render),
 * including rotation, resize and the mouse pointer (successor of the
 * src/render/text.c cursor-outside-the-screen fix 3eba08c96a).
 *
 * The display is a fake fbdev-like framebuffer on the heap: display_blend()
 * clips exactly like fbdev_display_blend() (src/video/fbdev_render.c) and then
 * copies the glyph, so out-of-bounds glyph reads and framebuffer writes are
 * caught by ASan.
 *
 * Input: [disp_w, disp_h, orientation, flags] then ops (VT chunks, rotate,
 * pointer, scrollback, resize).
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "font/font.h"
#include "render/render.h"
#include "shl/module.h"
#include "tsm.h"
#include "video/video.h"

extern struct kmscon_font_ops kmscon_font_8x16_ops;
extern struct render_ops render_bbulk_ops;

/* the 8x16 backend is linked statically with owner NULL */
void shl_module_ref(struct shl_module *module) { (void)module; }
void shl_module_unref(struct shl_module *module) { (void)module; }

/* ---- fake display (one per input) ---- */
static struct {
	unsigned int w, h;
	uint8_t *fb;
	bool need_redraw;
} fake;

void display_ref(struct display *disp) { (void)disp; }
void display_unref(struct display *disp) { (void)disp; }
unsigned int display_get_width(struct display *disp) { (void)disp; return fake.w; }
unsigned int display_get_height(struct display *disp) { (void)disp; return fake.h; }
bool display_need_redraw(struct display *disp) { (void)disp; return fake.need_redraw; }
bool display_has_damage(struct display *disp) { (void)disp; return false; }
bool display_supports_damage(struct display *disp) { (void)disp; return true; }
void display_set_cursor_offset(struct display *disp, int32_t x, int32_t y) { (void)disp; (void)x; (void)y; }
int display_show_cursor(struct display *disp, int32_t x, int32_t y) { (void)disp; (void)x; (void)y; return 0; }
void display_set_damage(struct display *disp, size_t n, struct video_rect *r)
{
	(void)disp;
	volatile uint32_t s = 0;
	for (size_t i = 0; i < n; i++) /* read every rect */
		s += (uint32_t)(uintptr_t)&r[i] + *(volatile uint8_t *)&r[i];
}
int display_clear(struct display *disp, uint8_t r, uint8_t g, uint8_t b)
{
	(void)disp; (void)g; (void)b;
	memset(fake.fb, r, (size_t)fake.w * fake.h);
	return 0;
}
int display_blend(struct display *disp, const struct video_blend_req *req)
{
	(void)disp;
	unsigned int width, height, tmp;
	if (!req || !req->buf)
		return -EINVAL;
	/* clipping identical to fbdev_display_blend() */
	width = req->w < req->buf->width ? req->w : req->buf->width;
	tmp = req->x + width;
	if (tmp < req->x || req->x >= fake.w)
		return -EINVAL;
	if (tmp > fake.w)
		width = fake.w - req->x;
	height = req->h < req->buf->height ? req->h : req->buf->height;
	tmp = req->y + height;
	if (tmp < req->y || req->y >= fake.h)
		return -EINVAL;
	if (tmp > fake.h)
		height = fake.h - req->y;
	uint8_t *dst = &fake.fb[(size_t)req->y * fake.w + req->x];
	const uint8_t *src = req->buf->data;
	while (height--) {
		for (unsigned int i = 0; i < width; i++)
			dst[i] = src[i] ? req->fr : req->br;
		dst += fake.w;
		src += req->buf->width;
	}
	return 0;
}

static void on_write(void *d, const char *b, size_t l) { (void)d; (void)b; (void)l; }
static const struct tsm_callbacks cbs = { .write = on_write };

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc; (void)argv;
	kmscon_font_8x16_ops.owner = NULL;
	if (kmscon_font_register(&kmscon_font_8x16_ops) || render_register(&render_bbulk_ops))
		abort();
	return 0;
}

static void resize_vte(struct render *rdr, struct tsm_vte *vte, struct kmscon_font *font)
{
	unsigned int cols = render_get_cols(rdr, font->width);
	unsigned int rows = render_get_rows(rdr, font->height);
	if (cols && rows) {
		tsm_vte_resize(vte, cols, rows);
		render_resize(rdr, cols, rows);
	}
}

static bool pointer;
static unsigned int ptr_x, ptr_y;

/* terminal.c's redraw: prepare, draw cells, draw the mouse pointer, render */
static void redraw(struct render *rdr, struct tsm_vte *vte, bool blink)
{
	struct tsm_attr attr;
	tsm_vte_get_default_attr(vte, &attr);
	render_prepare(rdr, &attr, blink);
	render_draw(rdr, vte, blink);
	if (pointer)
		render_draw_pointer(rdr, ptr_x, ptr_y);
	render_render(rdr);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct kmscon_font *font = NULL;
	struct render *rdr = NULL;
	struct tsm_vte *vte = NULL;
	if (size < 4)
		return -1;
	/* display from 0x0 to 1020x765, as a video backend can report it. A zero width
	 * or height is rejected by bbulk_set() itself (-EINVAL). */
	fake.w = data[0] * 4u;
	fake.h = data[1] * 3u;
	fake.need_redraw = data[3] & 1;
	pointer = false;
	fake.fb = calloc((size_t)fake.w * fake.h, 1);
	if (!fake.fb)
		return 0;
	if (kmscon_font_find(&font, "", 16, "8x16"))
		goto out;
	if (render_new(&rdr, "bbulk", (enum Orientation)(data[2] % 4), (struct display *)&fake))
		goto out;
	if (render_set(rdr, font))
		goto out;
	if (tsm_vte_new(&vte, &cbs, NULL))
		goto out;
	tsm_vte_set_max_scrollback(vte, 64);
	resize_vte(rdr, vte, font);
	bool blink = data[3] & 2;
	redraw(rdr, vte, blink);

	size_t i = 4;
	while (i < size) {
		uint8_t op = data[i++];
		size_t avail = size - i;
		if (op < 0xc0) { /* VT bytes */
			size_t n = (size_t)(op & 0x7f) + 1;
			if (n > avail)
				n = avail;
			tsm_vte_input(vte, (const char *)data + i, n);
			i += n;
		} else if (op < 0xd0) {
			render_rotate(rdr, (enum Orientation)(op % 4));
			resize_vte(rdr, vte, font);
		} else if (op < 0xe0) {
			if (avail < 2)
				break;
			pointer = true;
			ptr_x = data[i] * 4u;
			ptr_y = data[i + 1] * 4u;
			render_show_cursor(rdr, data[i] * 4, data[i + 1] * 4);
			i += 2;
		} else if (op < 0xf0) {
			if (op & 1)
				tsm_vte_sb_up(vte, op & 0xe);
			else
				tsm_vte_sb_down(vte, op & 0xe);
		} else {
			if (avail < 2)
				break;
			unsigned int c = 1 + data[i] % 200, r = 1 + data[i + 1] % 80;
			tsm_vte_resize(vte, c, r);
			render_resize(rdr, c, r);
			i += 2;
		}
		blink = !blink;
		redraw(rdr, vte, blink);
	}
out:
	if (vte)
		tsm_vte_free(vte);
	if (rdr)
		render_unref(rdr);
	if (font)
		kmscon_font_unref(font);
	free(fake.fb);
	fake.fb = NULL;
	return 0;
}
