#pragma once

#ifdef __cplusplus
extern "C" {
#endif
// Set size of window
void teenycode_set_bounds(int width, int height);
// Set fill style
void teenycode_set_fill(int r, int g, int b, int a);
// Set stroke style
void teenycode_set_stroke(int r, int g, int b, int a);
// Set line width
void teenycode_set_line_width(int w);
// Draw rectangle with specified fill and stroke
void teenycode_draw_rect(int x1, int y1, int x2, int y2);
// Draw rectangle over entire screen, ignore stroke
void teenycode_clear_screen();
// TODO paths
// TODO transformations
// Set font
void teenycode_set_font(char* name);
// Set font size
void teenycode_set_font_size(int size);
// Draw text (filled text)
void teenycode_draw_text(char* text, int x, int y);
// Draw image RGB
void teenycode_draw_rgb(char* image, int x, int y);
// Draw image RGBA
void teenycode_draw_rgba(char* image, int x, int y);

#ifdef __cplusplus
}
#endif

// All included teenycode functions with a guaranteed order
// Allows for better compression by conversion to number IDs
// clang-format off
#define TEENYCODE_INCLUDED_FUNCTIONS { \
	"teenycode_set_bounds",            \
	"teenycode_set_fill",              \
	"teenycode_set_stroke",            \
	"teenycode_set_line_width",        \
	"teenycode_draw_rect",             \
	"teenycode_clear_screen",          \
	"teenycode_set_font",              \
	"teenycode_set_font_size",         \
	"teenycode_draw_text",             \
	"teenycode_draw_rgb",              \
	"teenycode_draw_rgba",             \
}
// clang-format on

// TODO use wasi-sdk (wasi-libc)