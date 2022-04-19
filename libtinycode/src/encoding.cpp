#include <tinycode.hpp>

#include <iostream>

namespace TinyCode {
	namespace Encoding {
		void FixLastByte(uint64_t current_bit, std::vector<uint8_t>& bytes) {
			uint8_t remainder = current_bit % 8;

			if(remainder != 0) {
				// Shift last byte to the left
				bytes[bytes.size() - 1] <<= (8 - remainder);
			}
		}

		uint8_t GetRequiredBits(int64_t num) {
			num                   = std::abs(num);
			uint8_t bits_required = 0;
			do {
				bits_required++;
				num >>= 1;
			} while(num != 0);
			return bits_required;
		}

		uint64_t WriteNum(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t num_to_write = std::abs(num) << (64 - bit_size);
			bool sign_set        = false;
			while(true) {
				if(bit_size == 0) {
					return current_bit;
				}

				if(current_bit % 8 == 0) {
					bytes.push_back(0);
				} else {
					bytes[current_bit >> 3] <<= 1;
				}

				if(sign_set) {
					// Check whether most significant bit is set
					bytes[current_bit >> 3] |= (num_to_write & (1ULL << 63) ? 0x1 : 0x0);

					num_to_write <<= 1;
					bit_size--;
				} else {
					bytes[current_bit >> 3] |= (num < 0 ? 0x1 : 0x0);
					sign_set = true;
				}

				current_bit++;
			}
		}

		uint64_t WriteNumUnsigned(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t num_to_write = num << (64 - bit_size);
			while(true) {
				if(bit_size == 0) {
					return current_bit;
				}

				if(current_bit % 8 == 0) {
					bytes.push_back(0);
				} else {
					bytes[current_bit >> 3] <<= 1;
				}

				// Check whether most significant bit is set
				bytes[current_bit >> 3] |= (num_to_write & (1ULL << 63) ? 0x1 : 0x0);

				num_to_write <<= 1;
				bit_size--;
				current_bit++;
			}
		}

		uint64_t WriteTaggedNum(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			current_bit = WriteNumUnsigned(bit_size, 6, current_bit, bytes);
			return WriteNum(num, bit_size, current_bit, bytes);
		}

		uint64_t WriteTaggedNumUnsigned(
			int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			current_bit = WriteNumUnsigned(bit_size, 6, current_bit, bytes);
			return WriteNumUnsigned(num, bit_size, current_bit, bytes);
		}

		uint64_t WriteIdealIntegerList(std::vector<int64_t> data, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			// Determine best encoding scheme
			uint8_t max_bits_required_fixed           = 0;
			uint64_t total_bits_required_tagged       = LIST_TYPE_BITS + LIST_SIZE_BITS + 1;
			uint8_t max_bits_required_delta_fixed     = 0;
			uint64_t total_bits_required_delta_tagged = LIST_TYPE_BITS + LIST_SIZE_BITS + 1;
			int64_t last_num                          = 0;
			bool every_element_positive               = true;
			bool every_element_positive_delta         = true;
			for(int64_t num : data) {
				uint8_t bits_required = GetRequiredBits(num);
				if(bits_required > max_bits_required_fixed) {
					max_bits_required_fixed = bits_required;
				}
				if(num < 0) {
					every_element_positive = false;
				}
				total_bits_required_tagged += 6 + bits_required;

				uint8_t bits_required_delta = GetRequiredBits(num - last_num);
				if(bits_required_delta > max_bits_required_delta_fixed) {
					max_bits_required_delta_fixed = bits_required_delta;
				}
				if(num - last_num < 0) {
					every_element_positive_delta = false;
				}
				total_bits_required_delta_tagged += 6 + bits_required_delta;

				last_num = num;
			}

			uint64_t total_bits_required_fixed
				= LIST_TYPE_BITS + LIST_SIZE_BITS + 1 + 6 + data.size() * max_bits_required_fixed;
			uint64_t total_bits_required_delta_fixed
				= LIST_TYPE_BITS + LIST_SIZE_BITS + 1 + 6 + data.size() * max_bits_required_delta_fixed;

			if(!every_element_positive) {
				total_bits_required_tagged += data.size();
				total_bits_required_fixed += data.size();
			}

			if(!every_element_positive_delta) {
				total_bits_required_delta_tagged += data.size();
				total_bits_required_delta_fixed += data.size();
			}

			std::cout << "Fixed is " << (int)total_bits_required_fixed << std::endl;
			std::cout << "Tagged is " << (int)total_bits_required_tagged << std::endl;
			std::cout << "Delta fixed is " << (int)total_bits_required_delta_fixed << std::endl;
			std::cout << "Delta tagged is " << (int)total_bits_required_delta_tagged << std::endl;

			uint64_t min_bits = std::min(std::min(total_bits_required_fixed, total_bits_required_tagged),
				std::min(total_bits_required_delta_fixed, total_bits_required_delta_tagged));

			if(min_bits == total_bits_required_fixed) {
				std::cout << "Chosen fixed with " << (int)min_bits << std::endl;
				// List type
				current_bit = WriteNumUnsigned(IntegerListEncodingType::FIXED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = WriteNumUnsigned(every_element_positive, 1, current_bit, bytes);
				// Bits used for each number in list
				current_bit = WriteNumUnsigned(max_bits_required_fixed, 6, current_bit, bytes);

				for(int64_t num : data) {
					// Write number using bits specified earlier
					if(every_element_positive) {
						current_bit = WriteNumUnsigned(num, max_bits_required_fixed, current_bit, bytes);
					} else {
						current_bit = WriteNum(num, max_bits_required_fixed, current_bit, bytes);
					}
				}
			} else if(min_bits == total_bits_required_tagged) {
				std::cout << "Chosen tagged with " << (int)min_bits << std::endl;
				// List type
				current_bit = WriteNumUnsigned(IntegerListEncodingType::TAGGED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = WriteNumUnsigned(every_element_positive, 1, current_bit, bytes);

				for(int64_t num : data) {
					uint8_t bits_required = GetRequiredBits(num);

					// Write tagged number (number of bits used + number itself)
					if(every_element_positive) {
						current_bit = WriteTaggedNumUnsigned(num, bits_required, current_bit, bytes);
					} else {
						current_bit = WriteTaggedNum(num, bits_required, current_bit, bytes);
					}
				}
			} else if(min_bits == total_bits_required_delta_fixed) {
				std::cout << "Chosen delta fixed with " << (int)min_bits << std::endl;
				// List type
				current_bit
					= WriteNumUnsigned(IntegerListEncodingType::DELTA_FIXED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = WriteNumUnsigned(every_element_positive_delta, 1, current_bit, bytes);
				// Bits used for each number in list
				current_bit = WriteNumUnsigned(max_bits_required_delta_fixed, 6, current_bit, bytes);

				int64_t last_num = 0;
				for(int64_t num : data) {
					// Write number using bits specified earlier
					if(every_element_positive_delta) {
						current_bit
							= WriteNumUnsigned(num - last_num, max_bits_required_delta_fixed, current_bit, bytes);
					} else {
						current_bit = WriteNum(num - last_num, max_bits_required_delta_fixed, current_bit, bytes);
					}
					last_num = num;
				}
			} else if(min_bits == total_bits_required_delta_tagged) {
				std::cout << "Chosen delta tagged with " << (int)min_bits << std::endl;
				// List type
				current_bit
					= WriteNumUnsigned(IntegerListEncodingType::DELTA_TAGGED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = WriteNumUnsigned(every_element_positive_delta, 1, current_bit, bytes);

				int64_t last_num = 0;
				for(int64_t num : data) {
					int64_t num_delta     = num - last_num;
					uint8_t bits_required = GetRequiredBits(num_delta);

					// Write tagged number (number of bits used + number itself)
					if(every_element_positive_delta) {
						current_bit = WriteTaggedNumUnsigned(num_delta, bits_required, current_bit, bytes);
					} else {
						current_bit = WriteTaggedNum(num_delta, bits_required, current_bit, bytes);
					}
					last_num = num;
				}
			}

			return current_bit;
		}
	}
}