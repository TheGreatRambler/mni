#include "native_interface.hpp"

#include <tinycode.hpp>
#include <vector>

std::string NativeInterface::GetMessage() {
	std::vector<uint8_t> test { 0x0A, 0x0B };
	return TinyCode::Debug::PrintAsCArray(test);
}