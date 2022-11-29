#include <stdint.h>
#include <stdlib.h>

#include <mni/wasm/imports.h>

extern "C" {
__attribute__((used)) const char* mni_name() {
	return "mni.codes Hello";
}

constexpr int width     = 350;
constexpr int height    = 100;
constexpr int font_size = 60;

__attribute__((used)) bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_font_size(font_size);
	return true;
}

__attribute__((used)) bool mni_render(int64_t frame) {
	// Set fill for clearing the screen
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	// Get semi random color
	int r = frame % 255;
	int g = (frame + 127) % 255;
	int b = (frame + 50) % 255;

	// Draw centered text
	mni_set_fill(r, g, b, 255);
	int text_width = mni_get_text_width("Hello World!");
	mni_draw_text_fill("Hello World!", width / 2 - (text_width / 2), height / 2);
	return true;
}
}