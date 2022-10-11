#include <tinycode.hpp>
#include <tinycode/wasm/runtime.hpp>

#include <core/SkGraphics.h>
#include <gpu/GrBackendSurface.h>
#include <gpu/GrDirectContext.h>
#include <gpu/gl/GrGLInterface.h>
#include <thread>

#ifdef ANDROID
#include <android/log.h>
#endif

#define CREATE_IMPORT(name, func, functype)                                                        \
	func_##name = std::function(func);                                                             \
	wasmtime_linker_define_func(                                                                   \
		linker, "env", strlen("env"), "teenycode_" #name, strlen("teenycode_" #name), functype,    \
		[](void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t nargs,         \
			wasmtime_val_t* results, size_t nresults) -> wasm_trap_t* {                            \
			(*(decltype(func_##name)*)env)(args, results);                                         \
			return NULL;                                                                           \
		},                                                                                         \
		&func_##name, NULL);

#define GET_EXPORT(name)                                                                           \
	if(!wasmtime_instance_export_get(context, &instance, "teenycode_" #name,                       \
		   strlen("teenycode_" #name), &teenycode_##name)) {                                       \
		std::cerr << "Could not retrieve \""                                                       \
					 "teenycode_" #name "\" from exports"                                          \
				  << std::endl;                                                                    \
		return false;                                                                              \
	}

namespace TinyCode {
	namespace Wasm {
		bool Runtime::PrepareWindowStartup() {
			// TODO this crashes android with "Failed, there is no JavaVM"
			if(SDL_Init(SDL_INIT_VIDEO) != 0) {
				std::cerr << "Could not initialize SDL" << std::endl;
#ifdef ANDROID
				__android_log_print(ANDROID_LOG_ERROR, "TeenyCodes", "Could not initialize SDL: %s",
					SDL_GetError());
#endif
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

			// Create window at given size
			return PrepareWindow();
		}

		bool Runtime::PrepareWindow() {
			if(gl_context)
				SDL_GL_DeleteContext(gl_context);
			if(window)
				SDL_DestroyWindow(window);

			SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL);

			if(!(window = SDL_CreateWindow(meta.name.c_str(), SDL_WINDOWPOS_CENTERED,
					 SDL_WINDOWPOS_CENTERED, width, height, window_flags))) {
				std::cerr << "Could not create SDL window" << std::endl;
#ifdef ANDROID
				__android_log_print(ANDROID_LOG_ERROR, "TeenyCodes", "Could not create SDL window");
#endif
				return false;
			}

			if(!(gl_context = SDL_GL_CreateContext(window))) {
				std::cerr << "Could not create GL context" << std::endl;
#ifdef ANDROID
				__android_log_print(ANDROID_LOG_ERROR, "TeenyCodes", "Could not create GL context");
#endif
				return false;
			}

			if(SDL_GL_MakeCurrent(window, gl_context)) {
				std::cerr << "Could not make SDL window current" << std::endl;
#ifdef ANDROID
				__android_log_print(
					ANDROID_LOG_ERROR, "TeenyCodes", "Could not make SDL window current");
#endif
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
			buffer_info.fFBOID  = (GrGLuint)buffer;
			buffer_info.fFormat = GL_RGB8;

			GrBackendRenderTarget target(width, height, 0, 0, buffer_info);

			SkSurfaceProps props;
			surface = (SkSurface::MakeFromBackendRenderTarget(gr_context.get(), target,
				kBottomLeft_GrSurfaceOrigin, kRGB_888x_SkColorType, nullptr, &props));
			canvas  = surface->getCanvas();

#ifdef ANDROID
			if(!canvas) {
				__android_log_print(ANDROID_LOG_ERROR, "TeenyCodes", "Could not create canvas");
			}
#endif

			return true;
		}

		bool Runtime::AttachImports() {
			current_paint.setAntiAlias(false);

			CREATE_IMPORT(
				set_bounds,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32) {
						width  = args[0].of.i32;
						height = args[1].of.i32;
						// Only recreate if window has been created before
						if(window) {
							PrepareWindow();
						}
					}
				},
				ConstructFunction({ wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				set_color,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32) {
						current_color
							= SkColorSetRGB(args[0].of.i32, args[1].of.i32, args[2].of.i32);
						current_paint.setColor(current_color);
					}
				},
				ConstructFunction(
					{ wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				set_font,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					if(args[0].kind == WASM_I32) {
						current_font.setTypeface(SkTypeface::MakeFromName(
							(char*)memory_base + args[0].of.i32, SkFontStyle::Normal()));
					}
				},
				ConstructFunction({ wasm_valtype_new_i32() }))

			CREATE_IMPORT(
				clear_screen,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					canvas->clear(current_color);
				},
				ConstructFunction())

			CREATE_IMPORT(
				draw_string,
				[&](const wasmtime_val_t* args, wasmtime_val_t* results) -> void {
					if(args[0].kind == WASM_I32 && args[1].kind == WASM_I32
						&& args[2].kind == WASM_I32) {
						char* str = (char*)memory_base + args[2].of.i32;
						canvas->drawSimpleText(str, strlen(str), SkTextEncoding::kUTF8,
							args[0].of.i32, args[1].of.i32, current_font, current_paint);
					}
				},
				ConstructFunction(
					{ wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() }))

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
			// Run TeenyCode wasm on desktop
			engine = wasm_engine_new();

			store   = wasmtime_store_new(engine, NULL, NULL);
			context = wasmtime_store_context(store);
			linker  = wasmtime_linker_new(engine);

			wasmtime_module_t* module = NULL;
			wasmtime_module_new(engine, wasm_bytes.data(), wasm_bytes.size(), &module);

			AttachImports();

			// Create instance using imports
			wasmtime_error_t* error
				= wasmtime_linker_instantiate(linker, context, module, &instance, &trap);

			if(error) {
				wasm_name_t message;
				wasmtime_error_message(error, &message);
				std::cerr << "Error: " << std::string(message.data, message.data + message.size)
						  << std::endl;
			}

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

			wasmtime_val_t prepare_ret;
			// Don't check return yet
			wasmtime_func_call(
				context, &teenycode_prepare.of.func, NULL, 0, &prepare_ret, 1, &trap);

			GetMetadata();

			return true;
		}

		bool Runtime::GetMetadata() {
			wasmtime_val_t name_addr;
			wasmtime_func_call(context, &teenycode_name.of.func, NULL, 0, &name_addr, 1, &trap);
			int name_addr_value = name_addr.of.i32;

			meta = {
				.name = std::string((char*)(memory_base + name_addr_value)),
			};

			return true;
		}

		bool Runtime::TickWindow() {
			// TODO events
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

			wasmtime_val_t render_args[] = { WASM_I64_VAL(frame) };
			wasmtime_val_t render_ret;
			// Don't check return yet
			wasmtime_func_call(
				context, &teenycode_render.of.func, render_args, 1, &render_ret, 1, &trap);

			canvas->flush();
			SDL_GL_SwapWindow(window);
			frame++;

			return true;
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