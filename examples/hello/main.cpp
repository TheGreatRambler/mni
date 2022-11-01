#include <emscripten.h>
#include <math.h>
#include <mni/wasm/imports.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* mni_name() {
	return "mni.codes Hello";
}

constexpr int width     = 350;
constexpr int height    = 100;
constexpr int font_size = 60;

EMSCRIPTEN_KEEPALIVE
bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_font_size(font_size);
	return true;
}

EMSCRIPTEN_KEEPALIVE
bool mni_render(int64_t timestamp) {
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	int r = timestamp % 255;
	int g = (timestamp + 127) % 255;
	int b = (timestamp + 50) % 255;

	mni_set_fill(r, g, b, 255);
	int text_width = mni_get_text_width("Hello World!");
	mni_draw_text_fill("Hello World!", width / 2 - (text_width / 2), height / 2);
	return true;
}
}