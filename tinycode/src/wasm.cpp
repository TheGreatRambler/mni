#include <tinycode.hpp>

#include <pass.h>
#include <tools/optimization-options.h>
#include <wasm-binary.h>
#include <wasm-io.h>
#include <wasm.h>
#include <wasm3_cpp.h>

namespace TinyCode {
	namespace Wasm {
		void Optimize(std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
			wasm::Module wasm;

			wasm::WasmBinaryBuilder parser(wasm, wasm.features, (std::vector<char>&)in);
			parser.setDebugInfo(false);
			parser.setDWARF(false);
			parser.setSkipFunctionBodies(false);
			parser.read();

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