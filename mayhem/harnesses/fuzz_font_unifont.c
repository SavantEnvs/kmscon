/*
 * The built-in GNU Unifont backend (src/font/font_unifont.c with the generated
 * font_unifont_data from the meson build), registered with the real font core
 * (src/font/font.c, blocks.c) as kmscon does for font-engine=unifont. The data
 * is embedded, so the attacker-controlled part is what the terminal asks it to
 * draw: any codepoint the child prints, with bold/italic/underline, at the
 * configured font size. This reaches the sparse block search fixed in d8a3344381.
 *
 * Input: [height] then records of 4 bytes: 3-byte codepoint (up to 0xFFFFFF, so
 * invalid codepoints too) + 1 attribute byte.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "font/font.h"
#include "shl/module.h"

extern struct kmscon_font_ops kmscon_font_unifont_ops;

void shl_module_ref(struct shl_module *module) { (void)module; }
void shl_module_unref(struct shl_module *module) { (void)module; }

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc; (void)argv;
	kmscon_font_unifont_ops.owner = NULL;
	if (kmscon_font_register(&kmscon_font_unifont_ops))
		abort();
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct kmscon_font *font = NULL;
	if (size < 1)
		return -1;
	/* font-size from the config: 1..64 */
	if (kmscon_font_find(&font, "unifont", 1 + data[0] % 64, "unifont"))
		return 0;
	for (size_t i = 1; i + 4 <= size; i += 4) {
		uint32_t ch = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | data[i + 2];
		struct kmscon_font_attr attr = {
			.bold = data[i + 3] & 1, .italic = data[i + 3] & 2, .underline = data[i + 3] & 4,
		};
		kmscon_font_has_glyph(font, &attr, ch);
		struct kmscon_glyph *g = kmscon_font_render(font, &attr, ch);
		if (g) {
			/* read the whole bitmap, as the renderer's blit does */
			volatile uint8_t s = 0;
			for (size_t p = 0; p < (size_t)g->buf.width * g->buf.height; p++)
				s ^= g->buf.data[p];
		}
	}
	kmscon_font_unref(font);
	return 0;
}
