#include <emscripten.h>
#include <math.h>
#include <mni/wasm/imports.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* mni_name() {
	return "mni.codes Rotation";
}

constexpr int width     = 500;
constexpr int height    = 500;
constexpr int font_size = 60;

EMSCRIPTEN_KEEPALIVE
bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_font("Courier New");
	mni_set_font_size(font_size);
	mni_set_stroke(0, 0, 0, 255);
	return true;
}

EMSCRIPTEN_KEEPALIVE
bool mni_render(int64_t timestamp) {
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	char buf[4];
	int buf_size = sprintf(buf, "%d", mni_get_rotation());

	mni_set_fill(0, 0, 0, 255);
	int text_width = mni_get_text_width(buf);
	mni_draw_text_fill(buf, width / 2 - (text_width / 2), height / 2);
	return true;
}
}