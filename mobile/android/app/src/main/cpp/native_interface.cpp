#include "native_interface.hpp"

void NativeInterface::LoadCode(uint8_t* qr, size_t size) {
	// Copy into memory
	qr_bytes.assign(qr, qr + size);
	meta = TinyCode::Wasm::GetMetadata(qr_bytes);
}

std::string& NativeInterface::GetCodeName() {
	return meta.name;
}