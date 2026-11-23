/*
 * PSF1/PSF2 (optionally gzip'd) console font loading and glyph rendering:
 * src/font/font_psf.c registered with the real font core (src/font/font.c),
 * exactly as kmscon loads a font named by --font-name / font-engine=psf.
 * The input is the font file; every glyph lookup path (unicode table, block
 * elements, replacement char, scaling, bold/underline) is rendered and the font
 * is released through kmscon_font_unref (the destroy path of fix 62225169b5).
 */
#include <stdbool.h>
#include <stdint.h>
#include "font/font.h"
#include "shl/module.h"
#include "fuzz_common.h"

extern struct kmscon_font_ops kmscon_font_psf_ops;

/* font.c refcounts the backend's owning module; the backend is linked in
 * statically with owner NULL (as tests/test_font_freetype.c does). */
void shl_module_ref(struct shl_module *module) { (void)module; }
void shl_module_unref(struct shl_module *module) { (void)module; }

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc; (void)argv;
	kmscon_font_psf_ops.owner = NULL;
	if (kmscon_font_register(&kmscon_font_psf_ops))
		abort();
	return 0;
}

static const uint32_t probe[] = {
	0, ' ', 'A', 'z', 0x7f, 0xa0, 0xe9, 0x263a, 0x2500, 0x2588, 0x258f, 0x25a1,
	0x2591, 0xfffd, 0x1f600, 0x10ffff,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct kmscon_font *font = NULL;
	const char *path = fuzz_write_file(".psf", data, size);
	/* the requested height comes from the font name's config; use two sizes so
	 * both scale==1 and scale>1 are reached. */
	unsigned int height = (size & 1) ? 32 : 8;

	if (kmscon_font_find(&font, path, height, "psf"))
		return 0;

	for (unsigned int a = 0; a < 4; a++) {
		struct kmscon_font_attr attr = { .bold = a & 1, .underline = a & 2 };
		for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
			kmscon_font_has_glyph(font, &attr, probe[i]);
			kmscon_font_render(font, &attr, probe[i]);
		}
		/* plus codepoints taken from the tail of the input */
		for (size_t i = 0; i + 2 <= size && i < 64; i += 2) {
			uint32_t ch = (uint32_t)data[size - 1 - i] << 8 | data[size - 2 - i];
			kmscon_font_render(font, &attr, ch);
		}
	}
	kmscon_font_unref(font);
	return 0;
}
