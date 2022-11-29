#include <emscripten.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <mni/wasm/imports.h>

#define PHYSAC_NO_THREADS 1
#define PHYSAC_STANDALONE 1
#define PHYSAC_IMPLEMENTATION 1
#include "physac.h"

extern "C" {
__attribute__((used)) const char* mni_name() {
	return "mni.codes Physics";
}

constexpr int width     = 500;
constexpr int height    = 500;
constexpr int font_size = 60;
constexpr int radius    = 20;

__attribute__((used)) bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_font_size(font_size);
	mni_set_line_width(0);

	InitPhysics();

	return true;
}

int angle = 0;

__attribute__((used)) bool mni_render(int64_t frame) {
	// Set fill for clearing the screen
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	// Check if current rotation is valid before reading
	if(mni_has_rotation()) {
		angle = 90.0f - mni_get_rotation();
	}

	// Set gravity to correspond to user's phone rotation
	SetPhysicsGravity(
		40.0f * cos(angle * 3.14159f / 180.0f), 40.0f * sin(angle * 3.14159f / 180.0f));

	// Add circle every frame the user is holding click
	if(mni_is_pressed()) {
		PhysicsBody body = CreatePhysicsBodyCircle(
			Vector2 { mni_get_x_pressed(), mni_get_y_pressed() }, radius, 10);
		body->useGravity = true;
	}

	// Add a circle in the center of the screen every 10 frames
	if(frame % 10 == 0) {
		int x            = width / 2 + ((int)frame - 5) % 10;
		int y            = height / 2 + ((int)frame - 5) % 10;
		PhysicsBody body = CreatePhysicsBodyCircle(Vector2 { (float)x, (float)y }, radius, 10);
		body->useGravity = true;
	}

	// Step physics simulation one frame
	PhysicsStep();

	int bodies_count = GetPhysicsBodiesCount();

	for(int i = bodies_count - 1; i >= 0; i--) {
		PhysicsBody body = GetPhysicsBody(i);

		// Check if body is offscreen
		if(body->position.x < 0 - radius || body->position.x > width + radius
			|| body->position.y > height + radius) {
			DestroyPhysicsBody(body);
		} else {
			// Draw the body
			mni_set_fill((body->id * 100) % 255, (body->id * 75) % 255, (body->id * 50) % 255, 255);
			mni_draw_full_circle(body->position.x, body->position.y, radius);
		}
	}

	mni_set_fill(0, 0, 0, 255);
	char buf[4];
	int buf_size = sprintf(buf, "%d", bodies_count);
	mni_draw_text_fill(buf, 0, font_size);

	return true;
}
}