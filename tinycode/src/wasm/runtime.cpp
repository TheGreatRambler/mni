#include <tinycode.hpp>
#include <tinycode/wasm/runtime.hpp>

#include <core/SkGraphics.h>
#include <core/SkSurface.h>
#include <gpu/GrBackendSurface.h>
#include <gpu/GrDirectContext.h>
#include <gpu/gl/GrGLInterface.h>

namespace TinyCode {
	namespace Wasm {
		bool Runtime::PrepareWindow() {
			if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
				std::cerr << "Could not initialize SDL" << std::endl;
				return false;
			}

#if defined(__EMSCRIPTEN__)
			// WebGL1 + GL ES2
			const char* glsl_version = "#version 100";
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES2)
			// GL ES 2.0 + GLSL 100
			const char* glsl_version = "#version 100";
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
			// GL 3.2 Core + GLSL 150
			const char* glsl_version = "#version 150";
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
				SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on
														 // Mac
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
			// GL 3.0 + GLSL 130
			const char* glsl_version = "#version 130";
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

			// GL attributes
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
			SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

			SDL_WindowFlags window_flags
				= (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
									| SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);

			if(!(window = SDL_CreateWindow(meta.name.c_str(), SDL_WINDOWPOS_CENTERED,
					 SDL_WINDOWPOS_CENTERED, width, height, window_flags))) {
				std::cerr << "Could not create SDL window" << std::endl;
				return false;
			}

			if(!(gl_context = SDL_GL_CreateContext(window))) {
				std::cerr << "Could not create GL context" << std::endl;
				return false;
			}

			if(SDL_GL_MakeCurrent(window, gl_context)) {
				std::cerr << "Could not make SDL window current" << std::endl;
				return false;
			}

			glViewport(0, 0, width, height);
			glClearColor(1, 1, 1, 1);
			glClearStencil(0);
			glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

			auto gl_interface = GrGLMakeNativeInterface();

			sk_sp<GrDirectContext> gr_context(GrDirectContext::MakeGL(gl_interface));
			SkASSERT(gr_context);

			// Wrap FBO so skia can use it
			GrGLint buffer;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &buffer);

			GrGLFramebufferInfo buffer_info;
			buffer_info.fFBOID = (GrGLuint)buffer;
#if defined(ANDROID)
			buffer_info.fFormat = GL_RGB8_OES;
#else
			buffer_info.fFormat = GL_RGB8;
#endif

			GrBackendRenderTarget target(width, height, 0, 0, buffer_info);

			SkSurfaceProps props;
			sk_sp<SkSurface> surface(SkSurface::MakeFromBackendRenderTarget(gr_context.get(),
				target, kBottomLeft_GrSurfaceOrigin, kRGB_888x_SkColorType, nullptr, &props));
			canvas = surface->getCanvas();

			while(render) {
				canvas->clear(SK_ColorWHITE);

				SDL_Event event;
				while(SDL_PollEvent(&event)) {
					switch(event.type) {
					case SDL_KEYDOWN: {
						SDL_Keycode key = event.key.keysym.sym;
						if(key == SDLK_ESCAPE) {
							render = false;
						}
						break;
					}
					case SDL_QUIT:
						render = true;
						break;
					default:
						break;
					}
				}

				// TODO

				canvas->flush();
				SDL_GL_SwapWindow(window);
			}
		}

		bool Runtime::AttachImports() {
			wasm_functype_t* set_bounds_type
				= wasm_functype_new_2_0(wasm_valtype_new_i32(), wasm_valtype_new_i32());
			wasmtime_linker_define_func(
				linker, "env", strlen("env"), "teenycode_set_bounds",
				strlen("teenycode_set_bounds"), set_bounds_type,
				[](void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t nargs,
					wasmtime_val_t* results, size_t nresults) -> wasm_trap_t* {
					Runtime& runtime = *(Runtime*)env; // Simply ignore for time being
					std::cout << "set_bounds called!" << std::endl;
				},
				this, NULL);
		}

		bool Runtime::PrepareWasm() {
			// Run TeenyCode wasm on desktop
			engine = wasm_engine_new();

			store   = wasmtime_store_new(engine, NULL, NULL);
			context = wasmtime_store_context(store);
			linker  = wasmtime_linker_new(engine);

			wasmtime_module_t* module = NULL;
			wasmtime_module_new(engine, wasm_bytes.data(), wasm_bytes.size(), &module);

			AttachImports();

			wasm_trap_t* trap = NULL;
			wasmtime_instance_t instance;
			wasmtime_linker_instantiate(linker, context, module, &instance, &trap);

			char* item_name;
			size_t item_name_size;
			wasmtime_extern_t item;
			size_t export_index = 0;
			while(wasmtime_instance_export_nth(
				context, &instance, export_index, &item_name, &item_name_size, &item)) {
				if(item.kind == WASMTIME_EXTERN_MEMORY) {
					wasmtime_memory_t memory = item.of.memory;
					memory_base              = wasmtime_memory_data(context, &memory);
				}
				export_index++;
			}
		}

		bool Runtime::OpenWindow() {

			// TODO
		}

		bool Runtime::Close() {
			wasmtime_store_delete(store);
			wasm_engine_delete(engine);
			SDL_GL_DeleteContext(gl_context);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return true;
		}
	}
}