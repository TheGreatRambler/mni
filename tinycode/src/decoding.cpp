#include <tinycode.hpp>

#include <iostream>

namespace TinyCode {
	namespace Decoding {
		uint64_t ReadDataHeader(Encoding::DataHeader& header, std::vector<uint8_t>& bytes) {
			uint64_t current_bit = 0;
			size_t size;
			current_bit = ReadNumUnsigned(&size, 16, current_bit, bytes);
			header.size = size;
			// Resize given buffer to size just read
			// https://stackoverflow.com/a/9194117
			bytes.resize((current_bit + (size + 7) & ~7) >> 3);
			return current_bit;
		}

		uint64_t Read1Bit(bool* bit_out, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			*bit_out = bytes[current_bit >> 3] << (current_bit % 8) & 0b10000000;
			return current_bit + 1;
		}

		uint64_t ReadFloat(float* num_out, uint8_t removed_mantissa_bits, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			constexpr uint8_t float_mantissa_bits = 23;
			uint32_t num_bits;
			current_bit = ReadNumUnsigned(&num_bits, 32 - removed_mantissa_bits, current_bit, bytes);
			num_bits <<= removed_mantissa_bits;
			*num_out = *(float*)&num_bits;
		}

		uint64_t ReadDouble(double* num_out, uint8_t removed_mantissa_bits, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			constexpr uint8_t double_mantissa_bits = 52;
			uint64_t num_bits;
			current_bit = ReadNumUnsigned(&num_bits, 64 - removed_mantissa_bits, current_bit, bytes);
			num_bits <<= removed_mantissa_bits;
			*num_out = *(double*)&num_bits;
		}
	}
}