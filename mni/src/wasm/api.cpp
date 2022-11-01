// Contains only the imports used by mni codes

#include <mni.hpp>
#include <mni/wasm/runtime.hpp>

#include <core/SkBitmap.h>
#include <core/SkStream.h>

#define CREATE_IMPORT(name, func, functype)                                                        \
	func_##name = std::function(func);                                                             \
	wasmtime_linker_define_func(                                                                   \
		linker, "env", strlen("env"), "mni_" #name, strlen("mni_" #name), functype,                \
		[](void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t nargs,         \
			wasmtime_val_t* results, size_t nresults) -> wasm_trap_t* {                            \
			(void)caller;                                                                          \
			(void)nargs;                                                                           \
			(void)nresults;                                                                        \
			(*(decltype(func_##name)*)env)(args, results);                                         \
			return NULL;                                                                           \
		},                                                                                         \
		&func_##name, NULL);

namespace Mni {
	namespace Wasm {
		bool Runtime::AttachImports() {
			fill_paint.setAntiAlias(false);
			stroke_paint.setAntiAlias(false);
			fill_paint.setStyle(SkPaint::Style::kFill_Style);
			stroke_paint.setStyle(SkPaint::Style::kStroke_Style);
			stroke_paint.setStrokeWidth(12);

			CREATE_IMPORT(
				set_bounds,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32) {
						width  = args[0].of.i32;
						height = args[1].of.i32;
						// Only recreate if window has been created before
						if(have_window) {
							PrepareWindow();
						}
					}
				},
				ConstructFunction({ wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				set_fill,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32 && args[3].kind == WASM_I32) {
						fill_color = SkColorSetARGB(
							args[3].of.i32, args[0].of.i32, args[1].of.i32, args[2].of.i32);
						fill_paint.setColor(fill_color);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32(), wasm_valtype_new_i32(),
					wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				set_stroke,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32 && args[3].kind == WASM_I32) {
						stroke_color = SkColorSetARGB(
							args[3].of.i32, args[0].of.i32, args[1].of.i32, args[2].of.i32);
						stroke_paint.setColor(stroke_color);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32(), wasm_valtype_new_i32(),
					wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				set_line_width,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32) {
						stroke_paint.setStrokeWidth(args[0].of.i32);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				draw_rect,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32 && args[3].kind == WASM_I32) {
						auto dimensions = SkRect::MakeLTRB(
							args[0].of.i32, args[1].of.i32, args[2].of.i32, args[3].of.i32);
						canvas->drawRect(dimensions, fill_paint);
						canvas->drawRect(dimensions, stroke_paint);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32(), wasm_valtype_new_i32(),
					wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				clear_screen,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)args;
					(void)results;
					canvas->clear(fill_color);
				},
				ConstructFunction())

			CREATE_IMPORT(
				set_font,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32) {
						current_font.setTypeface(SkTypeface::MakeFromName(
							(char*)memory_base + args[0].of.i32, SkFontStyle::Normal()));
					}
				},
				ConstructFunction({ wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				set_font_size,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32) {
						current_font.setSize(args[0].of.i32);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				get_text_width,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					if(args[0].kind == WASM_I32) {
						char* str = (char*)memory_base + args[0].of.i32;
						int width
							= current_font.measureText(str, strlen(str), SkTextEncoding::kUTF8);
						results[0] = WASM_I32_VAL(width);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32() }, { wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				draw_text,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32) {
						char* str = (char*)memory_base + args[0].of.i32;
						canvas->drawSimpleText(str, strlen(str), SkTextEncoding::kUTF8,
							args[1].of.i32, args[2].of.i32, current_font, fill_paint);
						canvas->drawSimpleText(str, strlen(str), SkTextEncoding::kUTF8,
							args[1].of.i32, args[2].of.i32, current_font, stroke_paint);
					}
				},
				ConstructFunction(
					{ wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				draw_text_fill,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32) {
						char* str = (char*)memory_base + args[0].of.i32;
						canvas->drawSimpleText(str, strlen(str), SkTextEncoding::kUTF8,
							args[1].of.i32, args[2].of.i32, current_font, fill_paint);
					}
				},
				ConstructFunction(
					{ wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				draw_rgb,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32 && args[3].kind == WASM_I32
						&& args[4].kind == WASM_I32) {
						SkBitmap temp_bitmap;
						auto dimensions = SkISize::Make(args[1].of.i32, args[2].of.i32);
						auto info = SkColorInfo(kRGB_888x_SkColorType, kOpaque_SkAlphaType, NULL);
						temp_bitmap.setInfo(
							SkImageInfo::Make(dimensions, info), args[1].of.i32 * 3);
						temp_bitmap.setPixels(memory_base + args[0].of.i32);
						canvas->drawImage(temp_bitmap.asImage(), args[3].of.i32, args[4].of.i32);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32(), wasm_valtype_new_i32(),
					wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				draw_rgba,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					(void)results;
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32 && args[3].kind == WASM_I32
						&& args[4].kind == WASM_I32) {
						SkBitmap temp_bitmap;
						auto dimensions = SkISize::Make(args[1].of.i32, args[2].of.i32);
						auto info = SkColorInfo(kRGBA_8888_SkColorType, kPremul_SkAlphaType, NULL);
						temp_bitmap.setInfo(
							SkImageInfo::Make(dimensions, info), args[1].of.i32 * 4);
						temp_bitmap.setPixels(memory_base + args[0].of.i32);
						canvas->drawImage(temp_bitmap.asImage(), args[3].of.i32, args[4].of.i32);
					}
				},
				ConstructFunction({ wasm_valtype_new_i32(), wasm_valtype_new_i32(),
					wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				load_png,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32) {
						char* filename = (char*)memory_base + args[0].of.i32;
						std::unique_ptr<SkCodec> image
							= SkCodec::MakeFromStream(SkStream::MakeFromFile(filename));

						if(!image) {
							results[0] = WASM_I32_VAL(NULL);
							return;
						}

						auto info = image->getInfo();
						info.makeColorType(kRGBA_8888_SkColorType);
						// TODO
						// image->getPixels(info, )
					}
				},
				ConstructFunction(
					{ wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() },
					{ wasm_valtype_new_i32() }))

			return true;
		}
	}
}