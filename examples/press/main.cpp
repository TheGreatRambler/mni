#include <math.h>
#include <mni/wasm/imports.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
__attribute__((used)) const char* mni_name() {
	return "mni.codes Press";
}

constexpr int width  = 500;
constexpr int height = 500;

__attribute__((used)) bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_stroke(0, 0, 0, 255);
	mni_set_line_width(5);
	return true;
}

__attribute__((used)) bool mni_render(int64_t frame) {
	// Set fill for clearing the screen
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	// Draw black circle wherever the user is currently pressing
	if(mni_is_pressed()) {
		mni_set_fill(0, 0, 0, 255);
		mni_draw_full_circle(mni_get_x_pressed(), mni_get_y_pressed(), 50);
	}

	return true;
}
}