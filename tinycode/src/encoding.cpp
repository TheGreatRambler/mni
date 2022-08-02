#include <tinycode.hpp>
#include <tinycode/tree.hpp>

#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace TinyCode {
	namespace Encoding {
		uint64_t PrependSize(uint64_t current_bit, uint64_t size_current_bit, std::vector<uint8_t>& bytes) {
			// Writes LEB at size_current_bit with current_bit - size_current_bit
			uint64_t size  = current_bit - size_current_bit;
			auto size_bits = TinyCode::Encoding::GetRequiredLEBBits(size, DEFAULT_LEB_MULTIPLE);
			current_bit    = TinyCode::Encoding::MoveBits(size_current_bit, current_bit, size_current_bit + size_bits, bytes);
			return TinyCode::Encoding::WriteLEBUnsigned(size, DEFAULT_LEB_MULTIPLE, size_current_bit, bytes) + size;
		}

		void CopyOverSrcOffset(std::vector<uint8_t>& src, uint64_t size, uint64_t src_offset, std::vector<uint8_t>& dest) {
			uint64_t src_current_bit = src_offset;
			uint8_t offset_modulo    = src_offset % 8; //  & 7 is goated with the sauce
			uint8_t offset_inverse   = 8 - offset_modulo;

			do {
				if(offset_modulo == 0) {
					dest.push_back(src[src_current_bit >> 3]);
					src_current_bit += 8;
				} else {
					uint8_t result = src[src_current_bit >> 3] << offset_modulo;
					src_current_bit += 8;
					dest.push_back(result | (src[src_current_bit >> 3] >> offset_inverse));
				}
			} while(src_current_bit - src_offset < size);
		}

		uint64_t Write1Bit(bool bit, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			if(bytes.size() <= (current_bit >> 3)) {
				bytes.resize((current_bit >> 3) + 1);
			}

			bytes[current_bit >> 3] ^= (-!!bit ^ bytes[current_bit >> 3]) & (0b10000000 >> (current_bit % 8));
			return current_bit + 1;
		}

		uint64_t WriteFloat(float num, uint8_t mantissa_bits_to_remove, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			constexpr uint8_t float_mantissa_bits = 23;
			// Cast float into uint32 to remove mantissa bits
			uint32_t num_bits = *(uint32_t*)&num >> mantissa_bits_to_remove;
			return WriteNumUnsigned(num_bits, 32 - mantissa_bits_to_remove, current_bit, bytes);
		}

		uint64_t WriteDouble(double num, uint8_t mantissa_bits_to_remove, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			constexpr uint8_t double_mantissa_bits = 52;
			// Cast float into uint64_t to remove mantissa bits
			uint64_t num_bits = *(uint64_t*)&num >> mantissa_bits_to_remove;
			return WriteNumUnsigned(num_bits, 64 - mantissa_bits_to_remove, current_bit, bytes);
		}

		uint64_t MoveBits(uint64_t start, uint64_t end, uint64_t new_start, std::vector<uint8_t>& bytes) {
			auto size = end - start;

			if(new_start > start) {
				if(bytes.size() <= ((new_start + size) >> 3)) {
					bytes.resize(((new_start + size) >> 3) + 1);
				}

				for(uint64_t i = size; i > 0; i--) {
					bool bit;
					Decoding::Read1Bit(&bit, start + i - 1, bytes);
					Write1Bit(bit, new_start + i - 1, bytes);
				}
			} else if(new_start < start) {
				for(uint64_t i = 0; i < size; i++) {
					bool bit;
					Decoding::Read1Bit(&bit, start + i, bytes);
					Write1Bit(bit, new_start + i, bytes);
				}
			}

			return new_start + size;
		}

		uint64_t CopyBits(uint64_t start, uint64_t end, uint64_t new_start, std::vector<uint8_t>& bytes_src, std::vector<uint8_t>& bytes_dest) {
			auto size = end - start;
			for(uint64_t i = 0; i < size; i++) {
				bool bit;
				Decoding::Read1Bit(&bit, start + i, bytes_src);
				Write1Bit(bit, new_start + i, bytes_dest);
			}

			return new_start + size;
		}
	}
}