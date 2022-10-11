#include <emscripten.h>
#include <math.h>
#include <stdint.h>
#include <tinycode/wasm/imports.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* teenycode_name() {
	return "TeenyCodes Test";
}

EMSCRIPTEN_KEEPALIVE
bool teenycode_prepare() {
	teenycode_set_bounds(720, 1080);
	teenycode_set_font("Hack");
	teenycode_set_font_size(30);
	teenycode_set_stroke(0, 0, 0, 255);
	return true;
}

EMSCRIPTEN_KEEPALIVE
bool teenycode_render(int64_t timestamp) {
	teenycode_set_fill(0, 0, 0, 0);
	teenycode_clear_screen();
	// sin pulls in 6 kilobytes, find way to replace it
	int start_rect = 100 + sin(timestamp) * 100;
	teenycode_draw_rect(start_rect, start_rect, start_rect + 300, start_rect + 200);
	teenycode_set_fill(0, 0, 0, 255);
	teenycode_draw_text("WAHOOOOOOOOOOOOOOOOOOO", start_rect + 10, start_rect + 10);
	return true;
}
}