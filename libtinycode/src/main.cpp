#include <tinycode.hpp>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

//#include "fmt/core.h"

int main(int argc, char* argv[]) {
	//	int size      = 100000;
	//	uint8_t* data = (uint8_t*)malloc(size);
	//
	//	for(int i = 0; i < size; i++) {
	//		uint8_t temp_byte = data[i];
	//
	//		for(int j = 0; j < 8; j++) {
	//			bool bit_set = temp_byte & 0b00000001;
	//
	//			// TODO handle from here
	//
	//			temp_byte >>= 1;
	//		}
	//	}

	std::vector<uint8_t> bytes;
	TinyCode::Encoding::WriteNum(7, 6, 0, bytes);

	std::cout << bytes[0] << std::endl;

	return 0;
}

namespace TinyCode {
	namespace Instructions {
		Instruction::Instruction() { }
	}

	Parser::Parser(uint8_t* buf, size_t size) {
		uint8_t* data = (uint8_t*)malloc(size);

		for(int i = 0; i < size; i++) {
			uint8_t temp_byte = data[i];

			for(int j = 0; j < 8; j++) {
				bool bit_set = temp_byte & 0b00000001;

				// TODO handle from here

				temp_byte >>= 1;
			}
		}
	}
}