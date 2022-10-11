#pragma once

#define SK_GL
#define SK_RELEASE

#include <SDL.h>
#include <codec/SkCodec.h>
#include <core/SkCanvas.h>
#include <core/SkFont.h>
#include <core/SkSurface.h>
#include <functional>
#include <unordered_set>
#include <vector>
#include <wasm.h>
#include <wasmtime.h>

#if defined(__EMSCRIPTEN__)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include "imports.h"

namespace TinyCode {
	namespace Wasm {
		static const std::unordered_set<std::string> DEFINED_FUNCTIONS TEENYCODE_INCLUDED_FUNCTIONS;

		struct Metadata {
			std::string name;
		};

		class Runtime {
		public:
			Runtime(std::vector<uint8_t>& wasm_bytes)
				: wasm_bytes(wasm_bytes) {
				PrepareWasm();
			}

			bool PrepareWindowStartup();
			bool PrepareWasm();
			bool GetMetadata();
			bool TickWindow();
			bool Close();

			Metadata Meta() {
				return meta;
			}

		private:
			bool PrepareWindow();
			bool AttachImports();

			// Helper function to construct function types
			wasm_functype_t* ConstructFunction(std::vector<wasm_valtype_t*> params = {},
				std::vector<wasm_valtype_t*> results                               = {});

#define CREATE_IMPORT(name)                                                                        \
	std::function<void(const wasmtime_val_t* args, wasmtime_val_t* results)> func_##name;

			CREATE_IMPORT(set_bounds)
			CREATE_IMPORT(set_color)
			CREATE_IMPORT(set_font)
			CREATE_IMPORT(clear_screen)
			CREATE_IMPORT(draw_string)

#define CREATE_EXPORT(name) wasmtime_extern_t teenycode_##name;

			CREATE_EXPORT(prepare)
			CREATE_EXPORT(name)
			CREATE_EXPORT(render)

			std::vector<uint8_t>& wasm_bytes;
			// Default for wasm4
			int width { 400 };
			int height { 400 };
			bool render { true };
			int64_t frame { 0 };
			Metadata meta;

			SDL_Window* window { nullptr };
			SDL_GLContext gl_context { nullptr };

			sk_sp<SkSurface> surface;
			SkCanvas* canvas { nullptr };
			SkPaint current_paint;
			SkColor current_color;
			SkFont current_font;

			wasm_engine_t* engine { nullptr };
			wasmtime_store_t* store { nullptr };
			wasmtime_context_t* context { nullptr };
			wasmtime_linker_t* linker { nullptr };
			wasmtime_instance_t instance;
			wasm_trap_t* trap { nullptr };
			uint8_t* memory_base { nullptr };
		};
	}
}