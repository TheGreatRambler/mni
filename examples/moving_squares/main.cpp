#include <mni/wasm/imports.h>

#include <stdint.h>
#include <stdlib.h>

extern "C" {
__attribute__((used)) const char* mni_name() {
	return "mni.codes Moving Squares";
}

constexpr int width  = 500;
constexpr int height = 500;

struct moving_box {
	int r               = 0;
	int g               = 0;
	int b               = 0;
	int w               = 50;
	int h               = 50;
	int current_delta_x = 2;
	int current_delta_y = 2;
	int x               = width / 2 - 20;
	int y               = width / 2 + 20;
};

moving_box boxes[10];
int num_boxes           = 1;
bool currently_clicking = false;

__attribute__((used)) bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_stroke(0, 0, 0, 255);
	mni_set_line_width(2);
	boxes[0] = moving_box {};
	return true;
}

__attribute__((used)) bool mni_render(int64_t frame) {
	// Set fill for clearing the screen
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	for(int i = 0; i < 10; i++) {
		auto& box = boxes[i];
		mni_set_fill(box.r, box.g, box.b, 255);
		mni_draw_rect(box.x, box.y, box.x + box.w, box.y + box.h);

		int right_edge = box.x + box.current_delta_x + box.w;
		int left_edge  = box.x + box.current_delta_x;
		if(right_edge > width) {
			box.x = width - (right_edge - width) - box.w;
			box.current_delta_x *= -1;
		} else if(left_edge < 0) {
			box.x = -left_edge;
			box.current_delta_x *= -1;
		} else {
			box.x += box.current_delta_x;
		}
		int top_edge    = box.y + box.current_delta_y + box.h;
		int bottom_edge = box.y + box.current_delta_y;
		if(top_edge > height) {
			box.y = height - (top_edge - height) - box.h;
			box.current_delta_y *= -1;
		} else if(bottom_edge < 0) {
			box.y = -bottom_edge;
			box.current_delta_y *= -1;
		} else {
			box.y += box.current_delta_y;
		}
	}

	if(mni_is_pressed()) {
		if(!currently_clicking) {
			boxes[num_boxes % 10].x = mni_get_x_pressed();
			boxes[num_boxes % 10].y = mni_get_y_pressed();
			boxes[num_boxes % 10].r = frame % 255;
			boxes[num_boxes % 10].g = (frame + 127) % 255;
			boxes[num_boxes % 10].b = (frame + 50) % 255;
			num_boxes++;
			currently_clicking = true;
		}
	} else {
		currently_clicking = false;
	}

	return true;
}
}

/*
// Linked list version (runtime can't provide malloc so this version is ~5kb)
#include <mni/wasm/imports.h>

#include <stdint.h>
#include <stdlib.h>

extern "C" {
__attribute__((used)) const char* mni_name() {
	return "mni.codes Moving Squares";
}

constexpr int width  = 500;
constexpr int height = 500;

struct moving_box {
	int r               = 0;
	int g               = 0;
	int b               = 0;
	int w               = 50;
	int h               = 50;
	int current_delta_x = 2;
	int current_delta_y = 2;
	int x               = width / 2 - 20;
	int y               = width / 2 + 20;
	moving_box* next    = NULL;
};

moving_box* boxes       = NULL;
bool currently_clicking = false;

__attribute__((used)) bool mni_prepare() {
	mni_set_bounds(width, height);
	mni_set_stroke(0, 0, 0, 255);
	mni_set_line_width(2);
	boxes = new moving_box();
	return true;
}

__attribute__((used)) bool mni_render(int64_t frame) {
	// Set fill for clearing the screen
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	moving_box* box = boxes;
	while(box != NULL) {
		mni_set_fill(box->r, box->g, box->b, 255);
		mni_draw_rect(box->x, box->y, box->x + box->w, box->y + box->h);

		int right_edge = box->x + box->current_delta_x + box->w;
		int left_edge  = box->x + box->current_delta_x;
		if(right_edge > width) {
			box->x = width - (right_edge - width) - box->w;
			box->current_delta_x *= -1;
		} else if(left_edge < 0) {
			box->x = -left_edge;
			box->current_delta_x *= -1;
		} else {
			box->x += box->current_delta_x;
		}
		int top_edge    = box->y + box->current_delta_y + box->h;
		int bottom_edge = box->y + box->current_delta_y;
		if(top_edge > height) {
			box->y = height - (top_edge - height) - box->h;
			box->current_delta_y *= -1;
		} else if(bottom_edge < 0) {
			box->y = -bottom_edge;
			box->current_delta_y *= -1;
		} else {
			box->y += box->current_delta_y;
		}

		box = box->next;
	}

	if(mni_is_pressed()) {
		if(!currently_clicking) {
			moving_box* box = boxes;
			while(true) {
				if(box->next == NULL) {
					box->next    = new moving_box();
					box->next->x = mni_get_x_pressed();
					box->next->y = mni_get_y_pressed();
					box->next->r = frame % 255;
					box->next->g = (frame + 127) % 255;
					box->next->b = (frame + 50) % 255;
					break;
				} else {
					box = box->next;
				}
			}
			currently_clicking = true;
		}
	} else {
		currently_clicking = false;
	}

	return true;
}
}
*/