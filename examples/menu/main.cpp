#include <mni/wasm/imports.h>

#include <stdint.h>
#include <stdlib.h>

extern "C" {
__attribute__((used)) const char* mni_name() {
	return "mni.codes Menu";
}

constexpr int width     = 500;
constexpr int height    = 500;
constexpr int font_size = 22;

static const char* const dishes[] = {
	"Chicken Pot Pie",
	"Meatloaf",
	"Porridge",
	"Venison",
	"Bacon Burger",
	"Salmon",
	"Jerky",
	"Aspargus",
	"Steak",
	"Clam Chowder",
};
static const char* const descriptions[] = {
	"A heartwarming dish with a lot of chicken",
	"Make your beef last longer",
	"Not the greatest but not the worst",
	"A Colorado classic",
	"A great smoky flavor",
	"A 10 pound beast for the family",
	"Dry but good for a hike",
	"A delicate vegatable that tastes great",
	"A massive piece that'll take you all day",
	"A New England classic",
};
static const char* const prices[] = {
	"$11.45",
	"$12.30",
	"$4.23",
	"$15.50",
	"$13.40",
	"$35.60",
	"$3.70",
	"$7.80",
	"$25.60",
	"$15.60",
};
constexpr int num_dishes = 10;

int max_dish_size        = 0;
int max_description_size = 0;
int max_price_size       = 0;

bool currently_dragging   = false;
int drag_start_y          = 0;
int screen_offset_y_start = 0;
int screen_offset_y       = 0;

__attribute__((used)) bool mni_prepare() {
	mni_set_bounds(width, height);
	// mni_set_font("Hack");
	mni_set_font_size(font_size);

	for(int i = 0; i < num_dishes; i++) {
		int dish_size        = mni_get_text_width((char*)dishes[i]);
		int description_size = mni_get_text_width((char*)descriptions[i]);
		int price_size       = mni_get_text_width((char*)prices[i]);

		if(dish_size > max_dish_size)
			max_dish_size = dish_size;
		if(description_size > max_description_size)
			max_description_size = description_size;
		if(price_size > max_price_size)
			max_price_size = price_size;
	}

	return true;
}

__attribute__((used)) bool mni_render(int64_t frame) {
	// Set fill for clearing the screen
	mni_set_fill(255, 255, 255, 255);
	mni_clear_screen();

	if(mni_is_pressed()) {
		if(!currently_dragging) {
			drag_start_y          = mni_get_y_pressed();
			screen_offset_y_start = screen_offset_y;
			currently_dragging    = true;
		} else {
			screen_offset_y = screen_offset_y_start + mni_get_y_pressed() - drag_start_y;
		}
	} else {
		currently_dragging = false;
	}

	for(int i = 0; i < num_dishes; i++) {
		mni_set_fill(47, 72, 88, 255);
		mni_draw_text_fill((char*)dishes[i], 10, screen_offset_y + (font_size + 7) * (i * 2 + 1));
		mni_set_fill(51, 101, 138, 255);
		mni_draw_text_fill(
			(char*)prices[i], 30 + max_dish_size, screen_offset_y + (font_size + 7) * (i * 2 + 1));
		mni_set_fill(134, 87, 216, 255);
		mni_draw_text_fill(
			(char*)descriptions[i], 40, screen_offset_y + (font_size + 7) * (i * 2 + 2));
	}

	return true;
}
}