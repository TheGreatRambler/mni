#include <tinycode.hpp>

#include <iostream>

namespace TinyCode {
	namespace Decoding {
		uint64_t ReadNum(int64_t* num_out, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			bool is_negative = (bytes[current_bit >> 3] << (current_bit % 8)) & 0b10000000;
			current_bit++;

			int64_t out = 0;
			while(bit_size != 0) {
				out <<= 1;
				out |= ((bytes[current_bit >> 3] << (current_bit % 8)) & 0b10000000 ? 0x1 : 0x0);

				current_bit++;
				bit_size--;
			}

			if(is_negative) {
				out *= -1;
			}

			*num_out = out;
			return current_bit;
		}

		uint64_t ReadNumUnsigned(
			int64_t* num_out, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t out = 0;
			while(bit_size != 0) {
				out <<= 1;
				out |= ((bytes[current_bit >> 3] << (current_bit % 8)) & 0b10000000 ? 0x1 : 0x0);

				current_bit++;
				bit_size--;
			}

			*num_out = out;
			return current_bit;
		}

		uint64_t ReadTaggedNum(int64_t* num_out, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t bit_size;
			current_bit = ReadNumUnsigned(&bit_size, 6, current_bit, bytes);
			return ReadNum(num_out, (uint8_t)bit_size, current_bit, bytes);
		}

		uint64_t ReadTaggedNumUnsigned(int64_t* num_out, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t bit_size;
			current_bit = ReadNumUnsigned(&bit_size, 6, current_bit, bytes);
			return ReadNumUnsigned(num_out, (uint8_t)bit_size, current_bit, bytes);
		}

		uint64_t ReadIdealIntegerList(
			std::vector<int64_t>& data_out, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t list_type;
			current_bit = ReadNumUnsigned(&list_type, Encoding::LIST_TYPE_BITS, current_bit, bytes);
			int64_t list_size;
			current_bit = ReadNumUnsigned(&list_size, Encoding::LIST_SIZE_BITS, current_bit, bytes);
			int64_t every_element_positive;
			current_bit = ReadNumUnsigned(&every_element_positive, 1, current_bit, bytes);

			if(list_type == Encoding::FIXED) {
				int64_t bit_size;
				current_bit = ReadNumUnsigned(&bit_size, 6, current_bit, bytes);

				for(uint64_t i = 0; i < list_size; i++) {
					int64_t num;
					if(every_element_positive) {
						current_bit = ReadNumUnsigned(&num, bit_size, current_bit, bytes);
					} else {
						current_bit = ReadNum(&num, bit_size, current_bit, bytes);
					}
					data_out.push_back(num);
				}
			} else if(list_type == Encoding::TAGGED) {
				for(uint64_t i = 0; i < list_size; i++) {
					int64_t num;
					if(every_element_positive) {
						current_bit = ReadTaggedNumUnsigned(&num, current_bit, bytes);
					} else {
						current_bit = ReadTaggedNum(&num, current_bit, bytes);
					}
					data_out.push_back(num);
				}
			} else if(list_type == Encoding::DELTA_FIXED) {
				int64_t bit_size;
				current_bit = ReadNumUnsigned(&bit_size, 6, current_bit, bytes);

				int64_t last_num = 0;
				for(uint64_t i = 0; i < list_size; i++) {
					int64_t num;
					if(every_element_positive) {
						current_bit = ReadNumUnsigned(&num, bit_size, current_bit, bytes);
					} else {
						current_bit = ReadNum(&num, bit_size, current_bit, bytes);
					}
					data_out.push_back(num + last_num);
					last_num += num;
				}
			} else if(list_type == Encoding::DELTA_TAGGED) {
				int64_t last_num = 0;
				for(uint64_t i = 0; i < list_size; i++) {
					int64_t num;
					if(every_element_positive) {
						current_bit = ReadTaggedNumUnsigned(&num, current_bit, bytes);
					} else {
						current_bit = ReadTaggedNum(&num, current_bit, bytes);
					}
					data_out.push_back(num + last_num);
					last_num += num;
				}
			}

			return current_bit;
		}
	}
}