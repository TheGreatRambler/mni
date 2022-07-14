#include <gtest/gtest.h>
#include <tinycode.hpp>
#include <tinycode/wasm/parser.hpp>
#include <wasm-tools.h>

#include <fstream>
#include <random>
#include <vector>

// Test optimizing to bitcode (teenycode)
TEST(Wasm, OptimizeTiny) {
	wasm_tools_byte_vec_t module;
	std::mt19937 rng(1);
	std::uniform_int_distribution<int> dist(1, 255);

	constexpr int NUM_MODULES  = 1000;
	constexpr int SIZE_MODULES = 10000;

	for(int i = 0; i < NUM_MODULES; i++) {
		char seed[SIZE_MODULES];
		for(int j = 0; j < SIZE_MODULES; j++) {
			seed[j] = dist(rng) & 0xFF;
		}

		// Generate module with default config
		if(!wasm_smith_create(seed, SIZE_MODULES, &module)) {
			std::vector<uint8_t> data(module.data, module.data + module.size);
			// Feed to parser, check if no crash
			TinyCode::Wasm::WasmToOptimized(data, 0, {});
			wasm_tools_byte_vec_delete(&module);
		}
	}
}