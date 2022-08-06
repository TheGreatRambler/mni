#pragma once

#define SK_GL
#define SK_RELEASE

#include <SDL.h>
#include <codec/SkCodec.h>
#include <core/SkCanvas.h>
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
		class Runtime {
		public:
			Runtime(std::vector<uint8_t>& wasm_bytes)
				: wasm_bytes(wasm_bytes)
				, meta(GetMetadata(wasm_bytes)) { }

			bool PrepareWindow();
			bool PrepareWasm();
			bool OpenWindow();
			bool Close();

			// Used by imports
			SkCanvas* GetCanvas() {
				return canvas;
			}

		private:
			bool AttachImports();

			std::vector<uint8_t>& wasm_bytes;
			int width { 100 };
			int height { 100 };
			bool render { true };
			TeenyCodeMetadata meta;
			SDL_Window* window { nullptr };
			SDL_GLContext gl_context { nullptr };
			SkCanvas* canvas { nullptr };
			wasm_engine_t* engine { nullptr };
			wasmtime_store_t* store { nullptr };
			wasmtime_context_t* context { nullptr };
			wasmtime_linker_t* linker { nullptr };
			uint8_t* memory_base { nullptr };
		};
	}
}