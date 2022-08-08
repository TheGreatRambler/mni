#include "native_interface.hpp"

void NativeInterface::LoadCode(uint8_t* qr, size_t size) {
	std::vector optimized(qr, qr + size);
	TinyCode::Wasm::OptimizedToNormal(qr_bytes, 0, optimized);
	// Get metadata for later
	meta = TinyCode::Wasm::Runtime(qr_bytes).Meta();
}

std::string& NativeInterface::GetCodeName() {
	return meta.name;
}