#pragma once

#define SK_GL
#define SK_RELEASE

#include <SDL.h>
#include <array>
#include <codec/SkCodec.h>
#include <core/SkCanvas.h>
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

namespace TinyCode {
	namespace Wasm {
		static const std::unordered_set<std::string> DEFINED_FUNCTIONS {
			// Exports
			"teenycode_name",
			"teenycode_prepare",
			"teenycode_render",
		};

		struct Metadata {
			std::string name;
		};

		class Runtime {
		public:
			Runtime(std::vector<uint8_t>& wasm_bytes)
				: wasm_bytes(wasm_bytes) {
				PrepareWasm();
			}

			bool PrepareWindow();
			bool PrepareWasm();
			bool GetMetadata();
			bool OpenWindow();
			bool Close();

			Metadata Meta() {
				return meta;
			}

		private:
			bool AttachImports();

#define CREATE_IMPORT(name)                                                                        \
	std::function<void(const wasmtime_val_t* args, wasmtime_val_t* results)> func_##name;

			CREATE_IMPORT(set_bounds)
			CREATE_IMPORT(set_color)
			CREATE_IMPORT(set_font)
			CREATE_IMPORT(clear_screen)
			CREATE_IMPORT(draw_string)

			std::vector<uint8_t>& wasm_bytes;
			int width { 100 };
			int height { 100 };
			bool render { true };
			int64_t frame { 0 };
			Metadata meta;
			SDL_Window* window { nullptr };
			SDL_GLContext gl_context { nullptr };
			SkCanvas* canvas { nullptr };
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