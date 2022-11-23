#include <mni.hpp>
#include <mni/wasm/runtime.hpp>

#include <core/SkGraphics.h>
#include <fstream>
#include <gpu/GrBackendSurface.h>
#include <gpu/GrDirectContext.h>
#include <gpu/gl/GrGLInterface.h>
#include <thread>
#include <wasi.h>

#ifdef ANDROID
#include <android/log.h>
#endif

#define GET_EXPORT(name)                                                                           \
	if(!wasmtime_instance_export_get(                                                              \
		   context, &instance, "mni_" #name, strlen("mni_" #name), &mni_##name)) {                 \
		std::cerr << "Could not retrieve \""                                                       \
					 "mni_" #name "\" from exports"                                                \
				  << std::endl;                                                                    \
	} else {                                                                                       \
		name##_loaded = true;                                                                      \
	}

namespace Mni {
	namespace Wasm {
		bool Runtime::PrepareWindowStartup() {
#ifndef ANDROID
			if(SDL_Init(SDL_INIT_VIDEO) != 0) {
				std::cerr << "Could not initialize SDL" << std::endl;
				return false;
			}

#if defined(__EMSCRIPTEN__)
			// WebGL1 + GL ES2
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES2)
			// GL ES 2.0 + GLSL 100
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
			// GL 3.2 Core + GLSL 150
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
				SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on
														 // Mac
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
			// GL 3.0 + GLSL 130
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
#endif

			// Create window at given size
			return PrepareWindow();
		}

		bool Runtime::PrepareWindow() {
#ifndef ANDROID
			if(gl_context)
				SDL_GL_DeleteContext(gl_context);
			if(window)
				SDL_DestroyWindow(window);

			SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL);

			if(!(window = SDL_CreateWindow(meta.name.c_str(), SDL_WINDOWPOS_CENTERED,
					 SDL_WINDOWPOS_CENTERED, width, height, window_flags))) {
				std::cerr << "Could not create SDL window" << std::endl;
				have_window = false;
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
#endif

			auto gl_interface = GrGLMakeNativeInterface();
			sk_sp<GrDirectContext> gr_context(GrDirectContext::MakeGL(gl_interface));
			SkASSERT(gr_context);

			// Wrap FBO so skia can use it
			GrGLint buffer;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &buffer);

			GrGLFramebufferInfo buffer_info;
			buffer_info.fFBOID  = (GrGLuint)buffer;
			buffer_info.fFormat = GL_RGBA8;

			GrBackendRenderTarget target(width, height, 0, 0, buffer_info);

			SkSurfaceProps props;
			surface = SkSurface::MakeFromBackendRenderTarget(gr_context.get(), target,
				kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, nullptr, &props);
			canvas  = surface->getCanvas();

			have_window = true;

			return true;
		}

		bool Runtime::HandleErrors() {
			if(error) {
				wasm_name_t message;
				wasmtime_error_message(error, &message);
				wasmtime_error_delete(error);
				std::cerr << "Wasmtime Error: "
						  << std::string(message.data, message.data + message.size) << std::endl;
				wasm_byte_vec_delete(&message);
				return false;
			}

			return true;
		}

		wasm_functype_t* Runtime::ConstructFunction(
			std::vector<wasm_valtype_t*> params, std::vector<wasm_valtype_t*> results) {
			wasm_valtype_vec_t params_vec, results_vec;
			wasm_valtype_vec_new(&params_vec, params.size(), params.data());
			wasm_valtype_vec_new(&results_vec, results.size(), results.data());
			return wasm_functype_new(&params_vec, &results_vec);
		}

		bool Runtime::PrepareWasm() {
			engine = wasm_engine_new();

			store   = wasmtime_store_new(engine, NULL, NULL);
			context = wasmtime_store_context(store);
			linker  = wasmtime_linker_new(engine);

			wasmtime_module_t* module = NULL;
			error = wasmtime_module_new(engine, wasm_bytes.data(), wasm_bytes.size(), &module);
			HandleErrors();

			wasi_config_t* wasi_config = wasi_config_new();
			wasi_config_inherit_argv(wasi_config);
			wasi_config_inherit_env(wasi_config);
			wasi_config_inherit_stdin(wasi_config);
			wasi_config_inherit_stdout(wasi_config);
			wasi_config_inherit_stderr(wasi_config);
			error = wasmtime_context_set_wasi(context, wasi_config);
			HandleErrors();

			error = wasmtime_linker_define_wasi(linker);
			HandleErrors();
			AttachImports();

			// Create instance using imports
			error = wasmtime_linker_instantiate(linker, context, module, &instance, &trap);
			HandleErrors();

			// Find memory base
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

			GET_EXPORT(prepare)
			GET_EXPORT(name)
			GET_EXPORT(render)

			if(prepare_loaded) {
				wasmtime_val_t prepare_ret;
				// Don't check return yet
				wasmtime_func_call(context, &mni_prepare.of.func, NULL, 0, &prepare_ret, 1, &trap);
			}

			GetMetadata();

			return true;
		}

		bool Runtime::GetMetadata() {
			if(name_loaded) {
				wasmtime_val_t name_addr;
				wasmtime_func_call(context, &mni_name.of.func, NULL, 0, &name_addr, 1, &trap);
				int name_addr_value = name_addr.of.i32;
				meta.name           = std::string((char*)(memory_base + name_addr_value));
			}

			return true;
		}

		bool Runtime::TickWindow() {
// TODO events
#ifndef ANDROID
			SDL_Event event;
			while(SDL_PollEvent(&event)) {
				switch(event.type) {
				case SDL_KEYDOWN: {
					SDL_Keycode key = event.key.keysym.sym;
					if(key == SDLK_ESCAPE) {
						return false;
					}
					break;
				}
				case SDL_QUIT:
					return false;
				default:
					break;
				}
			}

			int x, y;
			uint32_t buttons;
			SDL_PumpEvents();
			buttons = SDL_GetMouseState(&x, &y);

			if((buttons & SDL_BUTTON_LMASK) != 0) {
				press_x = x;
				press_y = y;
			} else {
				press_x = -1.0f;
				press_y = -1.0f;
			}
#else
// Don't do anything for now
#endif

			if(render_loaded) {
				wasmtime_val_t render_args[] = { WASM_I64_VAL(frame) };
				wasmtime_val_t render_ret;
				// Don't check return yet
				wasmtime_func_call(
					context, &mni_render.of.func, render_args, 1, &render_ret, 1, &trap);
			} else {
				// Without the render function there is nothing to be run here
				return false;
			}

			canvas->flush();

#ifndef ANDROID
			SDL_GL_SwapWindow(window);
#endif

			frame++;

			return true;
		}

		bool Runtime::Close() {
			if(store)
				wasmtime_store_delete(store);
			if(engine)
				wasm_engine_delete(engine);
			surface = NULL;

#ifndef ANDROID
			SDL_Quit();
#endif
			return true;
		}
	}
}