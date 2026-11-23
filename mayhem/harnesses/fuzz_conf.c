/*
 * kmscon.conf parsing: the real option table (src/config.c kmscon_conf_new)
 * driven by the file parser (src/misc/conf.c conf_ctx_parse_file), i.e. what
 * kmscon_conf_load_main does with <configdir>/kmscon.conf. Typed option parsers
 * (mode=gpu[conn]WxH@R, screen=, grabs via xkbcommon keysym names, string
 * lists, vt=, login=) all run on fuzzed values. The parsed modes/screens are
 * then looked up with config_get_mode/config_get_screen as the video code does.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "conf.h"
#include "config.h"
#include "render/render.h"
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct conf_ctx *ctx = NULL;
	const char *path = fuzz_write_file(".conf", data, size);

	if (kmscon_conf_new(&ctx))
		return 0;
	if (conf_ctx_parse_file(ctx, "%s", path) == 0) {
		struct kmscon_conf_t *conf = conf_ctx_get_mem(ctx);
		unsigned int w, h, r;
		config_get_mode(conf, "pci-0000:00:02.0", "HDMI-A-1", &w, &h, &r);
		config_get_mode(conf, "", "", &w, &h, &r);
		enum Orientation o;
		bool en;
		config_get_screen(conf, "pci-0000:00:02.0", "HDMI-A-1", &o, &en);
	}
	kmscon_conf_free(ctx);
	return 0;
}
