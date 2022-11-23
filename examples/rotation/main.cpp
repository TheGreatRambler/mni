#include <math.h>
#include <mni/wasm/imports.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
__attribute__((used)) const char* mni_name() {
	return "mni.codes Rotation";
}

constexpr int width     = 500;
constexpr int height    = 500;
constexpr int font_size = 60;
int angle               = 0;

__attribute__((used)) bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_font("Courier New");
	mni_set_font_size(font_size);
	mni_set_stroke(0, 0, 0, 255);
	mni_set_line_width(5);
	return true;
}

__attribute__((used)) bool mni_render(int64_t frame) {
	// Set fill for clearing the screen
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	// Check if current rotation is valid before reading
	if(mni_has_rotation()) {
		angle = mni_get_rotation();
	}

	int center_x = width / 2;
	int center_y = height / 2;

	/*
		char buf[4];
		int buf_size = sprintf(buf, "%d", angle);

		mni_set_fill(0, 0, 0, 255);
		int text_width = mni_get_text_width(buf);
		mni_draw_text_fill(buf, center_x - (text_width / 2), center_y);
	*/

	// Draw an arc representing the current rotation
	mni_set_fill(0, 0, 0, 0);
	mni_draw_circle(center_x, center_y, 150, 90.0f + 30.0f - angle, -60.0f);

	return true;
}
}