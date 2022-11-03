#include <emscripten.h>
#include <math.h>
#include <mni/wasm/imports.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* mni_name() {
	return "mni.codes Gravity";
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

bool has_pressed = false;
int angle        = 0;
int x            = width / 2;
int y            = width / 2;
float vel_x      = 0;
float vel_y      = 0;

EMSCRIPTEN_KEEPALIVE
bool mni_render(int64_t timestamp) {
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	if(mni_is_pressed() && !has_pressed) {
		x           = mni_get_x_pressed();
		y           = mni_get_y_pressed();
		vel_x       = 0;
		vel_y       = 0;
		has_pressed = true;
	} else {
		has_pressed = false;
	}

	if(mni_has_rotation()) {
		angle = mni_get_rotation();
	}

	constexpr int PI = 3.14159;

	float hypot = sqrt(pow(vel_x, 2) + pow(vel_y, 2)) + 0.002;
	vel_x += cos((90.0f - (float)angle) * PI / 180) * hypot;
	vel_y += sin((90.0f - (float)angle) * PI / 180) * hypot;
	x += vel_x;
	y += vel_y;

	mni_set_fill(0, 0, 0, 255);
	mni_draw_full_circle(x, y, 50);

	return true;
}
}