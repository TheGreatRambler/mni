#include <tinycode.hpp>
#include <tinycode/wasm/parser.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include <ir/table-utils.h>
#include <m3_env.h>
#include <pass.h>
#include <tools/optimization-options.h>
#include <wasm-binary.h>
#include <wasm-debug.h>
#include <wasm-io.h>
#include <wasm-stack.h>
#include <wasm-type.h>
#include <wasm.h>
#include <wasm3.h>
#include <wasmtime.h>

// Finds all reachable functions in wasm module
// Based on
// https://github.com/WebAssembly/binaryen/blob/5881b541a4b276dcd5576aa065e4fb860531fc7b/src/ast_utils.h#L73
class DirectCallGraphAnalyzer
	: public wasm::PostWalker<DirectCallGraphAnalyzer, wasm::Visitor<DirectCallGraphAnalyzer>> {
public:
	DirectCallGraphAnalyzer(wasm::Module& module, const std::vector<wasm::Function*>& root)
		: module(module) {
		for(auto* curr : root) {
			queue.push_back(curr);
		}
		while(queue.size()) {
			auto* curr = queue.back();
			queue.pop_back();
			if(reachable.count(curr) == 0) {
				reachable.insert(curr);
				walk(curr->body);
			}
		}
	}

	void visitCall(wasm::Call* curr) {
		auto* target = module.getFunction(curr->target);
		if(reachable.count(target) == 0) {
			queue.push_back(target);
		}
	}

	std::unordered_set<wasm::Function*>& get_reachable() {
		return reachable;
	}

private:
	wasm::Module& module;
	std::vector<wasm::Function*> queue;
	std::unordered_set<wasm::Function*> reachable;
};

namespace TinyCode {
	namespace Wasm {
		void RemoveUnneccesaryInternal(wasm::Module& wasm, std::vector<uint8_t>& in,
			std::unordered_set<std::string> kept_names) {
			wasm::WasmBinaryBuilder parser(wasm, wasm.features, (std::vector<char>&)in);
			parser.setDebugInfo(false);
			parser.setDWARF(false);
			parser.setSkipFunctionBodies(false);
			parser.read();

			std::unordered_set<wasm::Name> kept_functions;
			for(auto& name : kept_names) {
				kept_functions.insert(wasm::Name(name));
			}

			std::vector<wasm::Function*> root_functions;
			for(auto& curr : wasm.exports) {
				if(curr->kind == wasm::ExternalKind::Function && kept_functions.count(curr->name)) {
					root_functions.push_back(wasm.getFunction(curr->value));
				}
			}

			// Remove unreachable and unused exports, like initialize
			DirectCallGraphAnalyzer analyzer(wasm, root_functions);
			wasm.removeExports([&](wasm::Export* func_export) {
				if(func_export->kind == wasm::ExternalKind::Function
					&& analyzer.get_reachable().count(wasm.getFunctionOrNull(func_export->value))
						   == 0) {
					return true;
				}
				return false;
			});

			wasm.removeExport("__indirect_function_table");

			wasm::PassOptions pass_options = { .debug = false,
				.validate                             = true,
				.validateGlobally                     = false,
				.optimizeLevel                        = 2,
				.shrinkLevel                          = 2,
				.inlining                             = { .partialInliningIfs = 4 },
				.trapsNeverHappen                     = true,
				.fastMath                             = true,
				.zeroFilledMemory                     = true,
				.debugInfo                            = false };

			auto runPasses = [&]() {
				wasm::PassRunner passRunner(&wasm, pass_options);
				passRunner.addDefaultOptimizationPasses();
				passRunner.run();
			};
			runPasses();
			if(true) {
				// Repeatedly run until binary does not decrease in size
				auto getSize = [&]() {
					wasm::BufferWithRandomAccess buffer;
					wasm::WasmBinaryWriter writer(&wasm, buffer);
					writer.setEmitModuleName(false);
					writer.setNamesSection(false);
					writer.write();
					return buffer.size();
				};
				auto lastSize = getSize();
				while(1) {
					runPasses();
					auto currSize = getSize();
					if(currSize >= lastSize) {
						break;
					}
					lastSize = currSize;
				}
			}

			// Can iterate through module to identify memory
			// TODO make name shorter or nonexistant
			// wasm.removeExport("memory");
		}

		void RemoveUnneccesary(std::vector<uint8_t>& in, std::vector<uint8_t>& out,
			std::unordered_set<std::string> kept_names) {
			wasm::Module wasm;
			RemoveUnneccesaryInternal(wasm, in, kept_names);

			wasm::BufferWithRandomAccess output_buffer;
			wasm::WasmBinaryWriter writer(&wasm, output_buffer);
			writer.setEmitModuleName(false);
			writer.setNamesSection(false);
			writer.write();

			std::copy(output_buffer.begin(), output_buffer.end(), std::back_inserter(out));
		}

		TeenyCodeMetadata GetMetadata(std::vector<uint8_t>& wasm) {
			wasm_engine_t* engine = wasm_engine_new();

			wasmtime_store_t* store     = wasmtime_store_new(engine, NULL, NULL);
			wasmtime_context_t* context = wasmtime_store_context(store);

			wasmtime_module_t* module = NULL;
			wasmtime_error_t* error
				= wasmtime_module_new(engine, wasm.data(), wasm.size(), &module);

			wasm_trap_t* trap = NULL;
			wasmtime_instance_t instance;
			error = wasmtime_instance_new(context, module, NULL, 0, &instance, &trap);

			uint8_t* memory_base = nullptr;

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

			// teenycode_name
			wasmtime_extern_t read_name;
			if(!wasmtime_instance_export_get(
				   context, &instance, "teenycode_name", strlen("teenycode_name"), &read_name)) {
				std::cerr << "Could not retrieve \"teenycode_name\" from exports" << std::endl;
				return {};
			}
			wasmtime_val_t name_addr;
			error = wasmtime_func_call(context, &read_name.of.func, NULL, 0, &name_addr, 1, &trap);
			int name_addr_value = name_addr.of.i32;

			TeenyCodeMetadata metadata = {
				.name = std::string((char*)(memory_base + name_addr_value)),
			};

			wasmtime_store_delete(store);
			wasm_engine_delete(engine);

			return metadata;
		}
	}
}