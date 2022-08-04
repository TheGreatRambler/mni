#include "native_interface.hpp"

void NativeInterface::LoadCode(uint8_t* qr, size_t size) {
	std::vector optimized(qr, qr + size);
	TinyCode::Wasm::OptimizedToNormal(qr_bytes, 0, optimized);
	// Copy into memory
	meta = TinyCode::Wasm::GetMetadata(qr_bytes);
}

std::string& NativeInterface::GetCodeName() {
	return meta.name;
}