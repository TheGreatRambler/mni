#include <tinycode.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include <pass.h>
#include <tools/optimization-options.h>
#include <wasm-binary.h>
#include <wasm-io.h>
#include <wasm.h>
#include <wasm3_cpp.h>

// Finds all reachable functions in wasm module
// Based on https://github.com/WebAssembly/binaryen/blob/5881b541a4b276dcd5576aa065e4fb860531fc7b/src/ast_utils.h#L73
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
		void Optimize(std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
			wasm::Module wasm;

			wasm::WasmBinaryBuilder parser(wasm, wasm.features, (std::vector<char>&)in);
			parser.setDebugInfo(false);
			parser.setDWARF(false);
			parser.setSkipFunctionBodies(false);
			parser.read();

			// Remove unused functions
			// Root functions are the only ones I call
			static std::unordered_set<wasm::Name> kept_functions = {
				"_Z15readable_stringv",
				"_Z9get_imagePhiiii",
			};

			std::vector<wasm::Function*> root_functions;
			for(auto& curr : wasm.exports) {
				if(curr->kind == wasm::ExternalKind::Function && kept_functions.count(curr->name)) {
					root_functions.push_back(wasm.getFunction(curr->value));
				}
			}

			// Remove unreachable functions, including unneeded exports
			DirectCallGraphAnalyzer analyzer(wasm, root_functions);
			wasm.removeExports([&](wasm::Export* func_export) {
				if(func_export->kind == wasm::ExternalKind::Function
					&& analyzer.get_reachable().count(wasm.getFunctionOrNull(func_export->value)) == 0) {
					std::cout << "Removing function: " << func_export->name << std::endl;
					return true;
				}
				return false;
			});

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

			wasm::BufferWithRandomAccess last_valid_buffer;

			auto runPasses = [&]() {
				wasm::PassRunner passRunner(&wasm, pass_options);
				passRunner.addDefaultOptimizationPasses();
				passRunner.run();
			};
			runPasses();
			if(true) {
				// Repeatedly run until binary does not decrease in size
				auto getSize = [&]() {
					last_valid_buffer.clear();
					wasm::WasmBinaryWriter writer(&wasm, last_valid_buffer);
					writer.setEmitModuleName(false);
					writer.setNamesSection(false);
					writer.write();
					return last_valid_buffer.size();
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

			std::copy(last_valid_buffer.begin(), last_valid_buffer.end(), std::back_inserter(out));
		}
	}
}