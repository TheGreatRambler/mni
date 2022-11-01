#include <emscripten.h>
#include <math.h>
#include <mni/wasm/imports.h>
#include <stdint.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* mni_name() {
	return "mni.codes Basic";
}

constexpr int width     = 500;
constexpr int height    = 500;
constexpr int font_size = 60;

EMSCRIPTEN_KEEPALIVE
bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_font("Hack");
	mni_set_font_size(font_size);
	mni_set_stroke(0, 0, 0, 255);
	return true;
}

EMSCRIPTEN_KEEPALIVE
bool mni_render(int64_t timestamp) {
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();
	// sin pulls in 6 kilobytes, find way to replace it
	int rect_size = sin(timestamp / 25.0) * 200 + 200;

	mni_set_line_width(10);
	mni_set_fill(0, 0, 255, 255);
	mni_draw_rect(100, height - rect_size, width - 100, height);

	int text_width = mni_get_text_width("Bouncy!");
	mni_set_line_width(1);
	mni_set_fill(0, 0, 0, 255);
	mni_draw_text("Bouncy!", width / 2 - (text_width / 2), height - rect_size - font_size / 2);
	return true;
}
}