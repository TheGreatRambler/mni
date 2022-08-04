#pragma once

#include <android/log.h>
#include <tinycode.hpp>

#include <cstdint>
#include <string>
#include <vector>

class NativeInterface {
public:
	NativeInterface() { }

	void LoadCode(uint8_t* qr, size_t size);
	std::string& GetCodeName();

private:
	std::vector<uint8_t> qr_bytes;
	TinyCode::Wasm::TeenyCodeMetadata meta;
};