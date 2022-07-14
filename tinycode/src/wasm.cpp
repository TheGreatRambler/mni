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
#include <tinycode/wasm/io.hpp>
#include <tools/optimization-options.h>
#include <wasm-binary.h>
#include <wasm-debug.h>
#include <wasm-io.h>
#include <wasm-stack.h>
#include <wasm-type.h>
#include <wasm.h>
#include <wasm3.h>

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
		void OptimizeInternal(
			wasm::Module& wasm, std::vector<uint8_t>& in, std::unordered_set<std::string> kept_names) {
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
					&& analyzer.get_reachable().count(wasm.getFunctionOrNull(func_export->value)) == 0) {
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

			wasm.removeExport("memory");
		}

		void Optimize(std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::unordered_set<std::string> kept_names) {
			wasm::Module wasm;
			OptimizeInternal(wasm, in, kept_names);
			/*
						std::vector<uint8_t> test_bytes;
						OptimizedWasmWriter writer2(&wasm, test_bytes, 0);
						writer2.setEmitModuleName(false);
						writer2.setNamesSection(false);
						uint64_t current_bit = writer2.analyzeAndWrite();

						wasm::Module wasm2;
						OptimizedWasmReader reader(wasm2, wasm2.features, test_bytes, 0);
						reader.setDebugInfo(false);
						reader.setDWARF(false);
						reader.setSkipFunctionBodies(false);
						reader.analyzeAndRead();
			*/
			wasm::BufferWithRandomAccess output_buffer;
			wasm::WasmBinaryWriter writer(&wasm, output_buffer);
			writer.setEmitModuleName(false);
			writer.setNamesSection(false);
			writer.write();

			// TinyCode::Wasm::WasmToOptimized(output_buffer, 0, {});
			TinyCode::Wasm::WasmToOptimized(in, 0, {});

			std::copy(output_buffer.begin(), output_buffer.end(), std::back_inserter(out));
		}

		uint64_t OptimizeTiny(std::vector<uint8_t>& in, std::unordered_set<std::string> kept_names,
			uint64_t current_bit, std::vector<uint8_t>& bytes) {
			wasm::Module wasm;
			OptimizeInternal(wasm, in, kept_names);

			// wasm::BufferWithRandomAccess output_buffer;
			// OptimizedWasmWriter writer(&wasm, output_buffer);
			// writer.setEmitModuleName(false);
			// writer.setNamesSection(false);
			// writer.write();

			// std::copy(output_buffer.begin(), output_buffer.end(), std::back_inserter(out));
		}

		void ConvertFromTiny(std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
			wasm::Module wasm;

			// OptimizedWasmReader reader(wasm, wasm.features, (std::vector<char>&)in);
			// reader.setDebugInfo(false);
			// reader.setDWARF(false);
			// reader.setSkipFunctionBodies(false);
			// reader.read();

			wasm::BufferWithRandomAccess output_buffer;
			wasm::WasmBinaryWriter writer(&wasm, output_buffer);
			writer.setEmitModuleName(false);
			writer.setNamesSection(false);
			writer.write();

			std::copy(output_buffer.begin(), output_buffer.end(), std::back_inserter(out));
		}

		void Execute(std::vector<uint8_t>& wasm) {

			// https://pastebin.com/js5Zn4DU
			M3Result result    = m3Err_none;
			IM3Environment env = m3_NewEnvironment();
			if(!env) {
				std::cerr << "Creating environment failed" << std::endl;
			}

			IM3Runtime runtime = m3_NewRuntime(env, 2056, NULL);
			if(!runtime) {
				std::cerr << "Creating runtime failed" << std::endl;
			}
			runtime->memoryLimit = 8388608;

			IM3Module module;
			result = m3_ParseModule(env, &module, wasm.data(), wasm.size());
			if(result) {
				std::cerr << "Creating module failed" << std::endl;
			}

			result = m3_LoadModule(runtime, module);
			if(result) {
				std::cerr << "Loading module failed" << std::endl;
			}

			IM3Function f;
			result = m3_FindFunction(&f, runtime, "_Z15readable_stringv");
			if(result) {
				std::cerr << "Loading test function failed" << std::endl;
			}

			result  = m3_CallV(f);
			int str = 0;
			result  = m3_GetResultsV(f, &str);

			uint32_t mem_size;
			uint8_t* mem = m3_GetMemory(runtime, &mem_size, 0);

			std::cout << "Returned string: " << std::string((char*)(mem + str)) << std::endl;

			m3_FreeRuntime(runtime);
			m3_FreeEnvironment(env);
		}

		void FuzzTest() {
			auto GenerateSeededRandomString = [](int seed, int len) {
				static constexpr auto chars = "0123456789"
											  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
											  "abcdefghijklmnopqrstuvwxyz";
				std::mt19937 rng(seed);
				auto dist   = std::uniform_int_distribution { {}, std::strlen(chars) - 1 };
				auto result = std::string(len, '\0');
				std::generate_n(begin(result), len, [&]() { return chars[dist(rng)]; });
				return result;
			};

			bool READ_COMPARE = std::filesystem::exists("moduleCompare.bin");
			std::fstream moduleCompare("moduleCompare.bin", READ_COMPARE
																? (std::ios::in | std::ios::binary)
																: (std::ios::out | std::ios::trunc | std::ios::binary));

			int64_t cumulativeTime = 0;

			for(int i = 0; i < 32767; i++) {
				std::string commandString
					= std::string("echo ") + GenerateSeededRandomString(i, 1000)
					  + std::string(
						  " | wasm-tools smith -o test.wasm --sign-extension-ops false --saturating-float-to-int false --multi-value false");
				system(commandString.c_str());

				try {
					wasm::Module wasm;
					std::ifstream testFile("test.wasm", std::ios::binary);
					std::vector<char> fileContents(
						(std::istreambuf_iterator<char>(testFile)), std::istreambuf_iterator<char>());

					auto start = std::chrono::high_resolution_clock::now();
					wasm::WasmBinaryBuilder parser(wasm, wasm.features, fileContents);
					parser.read();

					wasm::BufferWithRandomAccess buffer;
					wasm::WasmBinaryWriter writer(&wasm, buffer);
					writer.setEmitModuleName(false);
					writer.setNamesSection(false);
					writer.write();
					auto stop = std::chrono::high_resolution_clock::now();

					cumulativeTime += std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();

					if(READ_COMPARE) {
						int expectedSize;
						moduleCompare.read((char*)&expectedSize, sizeof(expectedSize));
						std::vector<uint8_t> expectedOutput(expectedSize);
						moduleCompare.read((char*)expectedOutput.data(), expectedSize);
						if(buffer != expectedOutput) {
							std::cout << "Module produces incorrect output:" << std::endl;
							system("wasm2wat test.wasm");
						}
					} else {
						int size = buffer.size();
						moduleCompare.write((char*)&size, sizeof(size));
						moduleCompare.write((char*)buffer.data(), size);
					}
				} catch(std::exception e) {
					std::cout << "Exception: " << e.what() << std::endl;
					system("wasm2wat test.wasm");
				}

				if(i % 100 == 0) {
					std::cout << "Checked " << i << " modules" << std::endl;
				}
			}

			std::cout << "Average is " << (double)cumulativeTime / 32767 << " microseconds" << std::endl;

			moduleCompare.close();
		}
	}
}