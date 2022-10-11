#include "native_interface.hpp"

void NativeInterface::LoadCode(uint8_t* qr, size_t size) {
	// Check if same QR code is not being read multiple times
	if(optimized_wasm_bytes.size() == 0
		|| (optimized_wasm_bytes.size() != size
			&& std::memcmp(optimized_wasm_bytes.data(), qr, size) != 0)) {
		optimized_wasm_bytes.assign(qr, qr + size);
		TinyCode::Wasm::OptimizedToNormal(wasm_bytes, 0, optimized_wasm_bytes);

		runtime = std::make_shared<TinyCode::Wasm::Runtime>(wasm_bytes);
		meta    = runtime->Meta();

		runtime->PrepareWindowStartup();

		while(runtime->TickWindow())
			void;
	}
}

std::string& NativeInterface::GetCodeName() {
	return meta.name;
}