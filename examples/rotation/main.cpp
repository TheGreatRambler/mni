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
int angle               = 0;

EMSCRIPTEN_KEEPALIVE
bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_font("Courier New");
	mni_set_font_size(font_size);
	mni_set_stroke(0, 0, 0, 255);
	mni_set_line_width(5);
	return true;
}

EMSCRIPTEN_KEEPALIVE
bool mni_render(int64_t timestamp) {
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	if(mni_has_rotation()) {
		angle = mni_get_rotation();
	}

	char buf[4];
	int buf_size = sprintf(buf, "%d", angle);

	int center_x = width / 2;
	int center_y = height / 2;
	mni_set_fill(0, 0, 0, 255);
	int text_width = mni_get_text_width(buf);
	mni_draw_text_fill(buf, center_x - (text_width / 2), center_y);

	mni_set_fill(0, 0, 0, 0);
	mni_draw_circle(center_x, center_y, 150, 90.0f + 30.0f - angle, -60.0f);

	return true;
}
}