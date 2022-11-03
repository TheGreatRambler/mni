#pragma once

#define SK_GL
#define SK_RELEASE

#include <codec/SkCodec.h>
#include <core/SkCanvas.h>
#include <core/SkFont.h>
#include <core/SkSurface.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <wasm.h>
#include <wasmtime.h>

#ifndef ANDROID
#include <SDL.h>
#endif

#ifdef ANDROID
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>
#elif defined(__EMSCRIPTEN__)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include "imports.h"

namespace Mni {
	namespace Wasm {
		// Reverse map at compile time
		// https://benpm.github.io/blog/how-to-invert/reverse/transform-a-map-at-compile-time-in-c++/
		template <typename Kout, typename Vout, typename Kin, typename Vin>
		std::unordered_map<Kout, Vout> _transformMap(const std::unordered_map<Kin, Vin>& inMap,
			const std::function<std::pair<Kout, Vout>(const std::pair<Kin, Vin>&)> mapfunc) {
			std::unordered_map<Kout, Vout> outMap;
			std::for_each(inMap.begin(), inMap.end(),
				[&outMap, &mapfunc](const std::pair<Kin, Vin>& p) { outMap.insert(mapfunc(p)); });
			return outMap;
		}

		static const std::unordered_map<int, std::string> DEFINED_FUNCTIONS MNI_INCLUDED_FUNCTIONS;
		static const std::unordered_map<std::string, int> REVERSE_DEFINED_FUNCTIONS = _transformMap(
			DEFINED_FUNCTIONS, std::function([](const std::pair<int, std::string>& p) {
				return std::make_pair(p.second, p.first);
			}));

		struct Metadata {
			std::string name;
		};

		class Runtime {
		public:
			Runtime(std::vector<uint8_t>& wasm_bytes)
				: wasm_bytes(wasm_bytes) { }

			bool PrepareWindowStartup();
			// Must call this first
			bool PrepareWasm();
			bool GetMetadata();
			bool TickWindow();
			bool Close();

			Metadata Meta() {
				return meta;
			}

			void SetRotation(int angle) {
				rotation = angle;
			}

			void SetPress(float x, float y) {
				press_x = x;
				press_y = y;
			}

		private:
			bool PrepareWindow();
			bool AttachImports();
			bool HandleErrors();

			// Helper function to construct function types
			wasm_functype_t* ConstructFunction(std::vector<wasm_valtype_t*> params = {},
				std::vector<wasm_valtype_t*> results                               = {});

#define DECLARE_IMPORT(name)                                                                       \
	std::function<void(const wasmtime_val_t* args, wasmtime_val_t* results)> func_##name;

			DECLARE_IMPORT(set_bounds)
			DECLARE_IMPORT(set_fill)
			DECLARE_IMPORT(set_stroke)
			DECLARE_IMPORT(set_line_width)
			DECLARE_IMPORT(draw_rect)
			DECLARE_IMPORT(draw_oval)
			DECLARE_IMPORT(draw_circle)
			DECLARE_IMPORT(draw_full_oval)
			DECLARE_IMPORT(draw_full_circle)
			DECLARE_IMPORT(clear_screen)
			DECLARE_IMPORT(set_font)
			DECLARE_IMPORT(set_font_size)
			DECLARE_IMPORT(get_text_width)
			DECLARE_IMPORT(draw_text)
			DECLARE_IMPORT(draw_text_fill)
			DECLARE_IMPORT(draw_rgb)
			DECLARE_IMPORT(draw_rgba)
			DECLARE_IMPORT(load_png)
			DECLARE_IMPORT(get_rotation)
			DECLARE_IMPORT(is_pressed)
			DECLARE_IMPORT(get_x_pressed)
			DECLARE_IMPORT(get_y_pressed)

#define DECLARE_EXPORT(name)                                                                       \
	wasmtime_extern_t mni_##name;                                                                  \
	bool name##_loaded = false;

			DECLARE_EXPORT(prepare)
			DECLARE_EXPORT(name)
			DECLARE_EXPORT(render)

			std::vector<uint8_t>& wasm_bytes;
			int width { 512 };
			int height { 512 };
			bool render { true };
			int64_t frame { 0 };
			Metadata meta;

			// Input variables
			int rotation { 0 };
			float press_x { -1.0f };
			float press_y { -1.0f };

			bool have_window { false };
#ifndef ANDROID
			SDL_Window* window { nullptr };
			SDL_GLContext gl_context { nullptr };
#endif

			sk_sp<SkSurface> surface;
			SkCanvas* canvas { nullptr };
			SkFont current_font;

			SkPaint fill_paint;
			SkColor fill_color;
			SkPaint stroke_paint;
			SkColor stroke_color;

			wasm_engine_t* engine { nullptr };
			wasmtime_store_t* store { nullptr };
			wasmtime_context_t* context { nullptr };
			wasmtime_linker_t* linker { nullptr };
			wasmtime_instance_t instance;
			wasm_trap_t* trap { nullptr };
			wasmtime_error_t* error { nullptr };
			uint8_t* memory_base { nullptr };
		};
	}
}