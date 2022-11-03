#include <emscripten.h>
#include <math.h>
#include <mni/wasm/imports.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* mni_name() {
	return "mni.codes Press";
}

constexpr int width  = 500;
constexpr int height = 500;

EMSCRIPTEN_KEEPALIVE
bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_stroke(0, 0, 0, 255);
	mni_set_line_width(5);
	return true;
}

EMSCRIPTEN_KEEPALIVE
bool mni_render(int64_t timestamp) {
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	if(mni_is_pressed()) {
		mni_set_fill(0, 0, 0, 255);
		mni_draw_full_circle(mni_get_x_pressed(), mni_get_y_pressed(), 50);
	}

	return true;
}
}