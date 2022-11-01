#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Set size of window
void mni_set_bounds(int width, int height);
// Set fill style
void mni_set_fill(int r, int g, int b, int a);
// Set stroke style
void mni_set_stroke(int r, int g, int b, int a);
// Set line width
void mni_set_line_width(int w);
// Draw rectangle with specified fill and stroke
void mni_draw_rect(int x1, int y1, int x2, int y2);
// Draw rectangle over entire screen, ignore stroke
void mni_clear_screen();
// TODO paths
// TODO transformations
// Set font
void mni_set_font(char* name);
// Set font size
void mni_set_font_size(int size);
// Get text width when rendered, taking into account set font size
int mni_get_text_width(char* text);
// Draw text with bottom left corner being coordinates (not including bottom of y etc)
void mni_draw_text(char* text, int x, int y);
// Draw text with no stroke, only fill
void mni_draw_text_fill(char* text, int x, int y);
// Draw image RGB
void mni_draw_rgb(uint8_t* image, int w, int h, int x, int y);
// Draw image RGBA
void mni_draw_rgba(uint8_t* image, int w, int h, int x, int y);
// Load PNG from path as RGBA, returning buffer containing image and the width and height
uint8_t* mni_load_png(char* image, int& w, int& h);

#ifdef __cplusplus
}
#endif

// All mni.codes functions with a guaranteed order
// Allows for better compression by conversion to number IDs
// clang-format off
#define MNI_INCLUDED_FUNCTIONS {      \
	{0,  "mni_prepare"},              \
	{1,  "mni_render"},               \
	{2,  "mni_name"},                 \
	{3,  "mni_set_bounds"},           \
	{4,  "mni_set_fill"},             \
	{5,  "mni_set_stroke"},           \
	{6,  "mni_set_line_width"},       \
	{7,  "mni_draw_rect"},            \
	{8,  "mni_clear_screen"},         \
	{9,  "mni_set_font"},             \
	{10, "mni_set_font_size"},        \
	{11, "mni_get_text_width"},       \
	{12, "mni_draw_text"},            \
	{13, "mni_draw_text_fill"},       \
	{14, "mni_draw_rgb"},             \
	{15, "mni_draw_rgba"},            \
	{16, "mni_load_png"},             \
}
// clang-format on

// TODO use wasi-sdk (wasi-libc)