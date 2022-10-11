#pragma once

#include <android/log.h>
#include <tinycode.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class NativeInterface {
public:
	NativeInterface() { }

	void LoadCode(uint8_t* qr, size_t size);
	std::string& GetCodeName();

private:
	std::vector<uint8_t> optimized_wasm_bytes;
	std::vector<uint8_t> wasm_bytes;
	TinyCode::Wasm::Metadata meta;
	std::shared_ptr<TinyCode::Wasm::Runtime> runtime;
};

static NativeInterface* interface;