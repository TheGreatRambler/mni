#include "native_interface.hpp"

#include <chrono>
#include <fstream>

bool NativeInterface::LoadFromBuffer(uint8_t* buffer, size_t size) {
	if(optimized_wasm_bytes.size() == 0
		|| (optimized_wasm_bytes.size() != size
			&& std::memcmp(optimized_wasm_bytes.data(), buffer, size) != 0)) {
		optimized_wasm_bytes.assign(buffer, buffer + size);
		Mni::Wasm::OptimizedToNormal(wasm_bytes, 0, optimized_wasm_bytes);

		runtime = std::make_shared<Mni::Wasm::Runtime>(wasm_bytes);

		if(!runtime->PrepareWasm()) {
			return false;
		}

		meta = runtime->Meta();
		runtime->PrepareWindowStartup();

		return true;
	}

	return false;
}

bool NativeInterface::RenderNextFrame() {
	bool success = runtime->TickWindow();

	if(!success) {
		runtime->Close();
	}

	return success;
}

std::string& NativeInterface::GetCodeName() {
	return meta.name;
}