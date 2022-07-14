#include <tinycode.hpp>
#include <tinycode/tree.hpp>

#include <bit>
#include <bitset>
#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace TinyCode {
	namespace Encoding {
		uint64_t AddDataHeader(uint64_t current_bit, std::vector<uint8_t>& bytes, DataHeader header) {
			// Header goes at beginning
			std::vector<uint8_t> output_header;
			WriteNumUnsigned(header.size, 16, 0, output_header);
			bytes.insert(bytes.begin(), output_header.begin(), output_header.end());
			return current_bit + output_header.size() * 8;
		}

		void CopyOverSrcOffset(
			std::vector<uint8_t>& src, uint64_t size, uint64_t src_offset, std::vector<uint8_t>& dest) {
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

		uint8_t GetRequiredBits(int64_t num) {
			// Must cast to unsigned but works regardless
			return std::bit_width<uint64_t>(std::abs(num));
		}

		uint8_t GetRequiredLEBBits(int64_t num, uint8_t multiple_bits) {
			uint8_t required_bits = GetRequiredBits(num);
			return std::ceil(required_bits / (float)multiple_bits) * (multiple_bits + 1);
		}

		uint64_t WriteNum(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			current_bit = Write1Bit(num < 0, current_bit, bytes);
			return WriteNumUnsigned(std::abs(num), bit_size, current_bit, bytes);
		}

		uint64_t WriteNumUnsigned(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t num_to_write = num << (64 - bit_size);
			while(bit_size != 0) {
				current_bit = Write1Bit(num_to_write & (1ULL << 63), current_bit, bytes);
				num_to_write <<= 1;
				bit_size--;
			}

			return current_bit;
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

		uint64_t Write1Bit(bool bit, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			if(bytes.size() <= (current_bit >> 3)) {
				bytes.resize((current_bit >> 3) + 1);
			}

			bytes[current_bit >> 3] ^= (-!!bit ^ bytes[current_bit >> 3]) & (0b10000000 >> (current_bit % 8));
			return current_bit + 1;
		}

		uint64_t WriteLEB(int64_t num, uint8_t multiple_bits, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			current_bit          = Write1Bit(num < 0, current_bit, bytes);
			num                  = std::abs(num);
			int8_t required_bits = GetRequiredBits(num);
			while(required_bits >= 0) {
				const uint64_t mask = (1UL << multiple_bits) - 1;
				current_bit         = WriteNumUnsigned(num & mask, multiple_bits, current_bit, bytes);
				num >>= multiple_bits;
				required_bits -= multiple_bits;

				current_bit = Write1Bit(required_bits < 0, current_bit, bytes);
			}
			return current_bit;
		}

		uint64_t WriteLEBUnsigned(
			int64_t num, uint8_t multiple_bits, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int8_t required_bits = GetRequiredBits(num);
			while(required_bits > 0) {
				const uint64_t mask = (1UL << multiple_bits) - 1;
				current_bit         = WriteNumUnsigned(num & mask, multiple_bits, current_bit, bytes);
				num >>= multiple_bits;
				required_bits -= multiple_bits;

				current_bit = Write1Bit(required_bits <= 0, current_bit, bytes);
			}
			return current_bit;
		}

		uint64_t WriteHuffmanHeader(std::vector<int64_t> data,
			std::unordered_map<int64_t, TinyCode::Tree::NodeRepresentation>& rep_map, uint64_t current_bit,
			std::vector<uint8_t>& bytes) {
			TinyCode::Tree::GenerateHuffman(data, rep_map);
			return WriteHuffmanHeader(rep_map, current_bit, bytes);
		}

		uint64_t WriteHuffmanHeader(std::unordered_map<int64_t, TinyCode::Tree::NodeRepresentation>& rep_map,
			uint64_t current_bit, std::vector<uint8_t>& bytes) {
			std::vector<int64_t> element_list;
			std::vector<TinyCode::Tree::NodeRepresentation> representation_list;
			for(auto& element : rep_map) {
				element_list.push_back(element.first);
				representation_list.push_back(element.second);
			}

			current_bit = WriteSimpleIntegerList(element_list, current_bit, bytes);

			for(int i = 0; i < element_list.size(); i++) {
				auto& rep   = representation_list[i];
				current_bit = WriteNumUnsigned(rep.bit_size, 6, current_bit, bytes);
				current_bit = WriteNumUnsigned(rep.representation, rep.bit_size, current_bit, bytes);
			}

			return current_bit;
		}

		uint64_t WriteLEBIntegerList(std::vector<int64_t> data, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			bool every_element_positive = true;
			for(int64_t num : data) {
				if(num < 0) {
					every_element_positive = false;
				}
			}

			// List size
			current_bit = WriteLEBUnsigned(data.size(), DEFAULT_LEB_MULTIPLE, current_bit, bytes);
			// Whether every element is positive
			current_bit = Write1Bit(every_element_positive, current_bit, bytes);

			for(int64_t num : data) {
				if(every_element_positive) {
					current_bit = WriteLEBUnsigned(num, DEFAULT_LEB_MULTIPLE, current_bit, bytes);
				} else {
					current_bit = WriteLEB(num, DEFAULT_LEB_MULTIPLE, current_bit, bytes);
				}
			}

			return current_bit;
		}

		// Simple works extremely well for tiny arrays, but not for general compression
		uint64_t WriteSimpleIntegerList(std::vector<int64_t> data, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			if(data.size() > (0x1 << LIST_SIZE_BITS)) {
				// TODO data array is too large for chosen list size bits, ask user to compile under different settings
			}

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

			// std::cout << "Fixed is " << (int)total_bits_required_fixed << std::endl;
			// std::cout << "Tagged is " << (int)total_bits_required_tagged << std::endl;
			// std::cout << "Delta fixed is " << (int)total_bits_required_delta_fixed << std::endl;
			// std::cout << "Delta tagged is " << (int)total_bits_required_delta_tagged << std::endl;

			uint64_t min_bits = std::min(std::min(total_bits_required_fixed, total_bits_required_tagged),
				std::min(total_bits_required_delta_fixed, total_bits_required_delta_tagged));

			if(min_bits == total_bits_required_fixed) {
				// std::cout << "Chosen fixed with " << (int)min_bits << std::endl;
				//  List type
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
				// std::cout << "Chosen tagged with " << (int)min_bits << std::endl;
				//  List type
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
				// std::cout << "Chosen delta fixed with " << (int)min_bits << std::endl;
				//  List type
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
				// std::cout << "Chosen delta tagged with " << (int)min_bits << std::endl;
				//  List type
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

		uint64_t WriteHuffmanIntegerList(std::vector<int64_t> data, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			if(data.size() > (0x1 << LIST_SIZE_BITS)) {
				// TODO data array is too large for chosen list size bits, ask user to compile under different settings
			}

			// List size
			current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);

			// std::unordered_map<int64_t, std::vector<int64_t>> cache;
			// int64_t cache[CACHE_SIZE];

			std::unordered_map<int64_t, TinyCode::Tree::NodeRepresentation> rep_map;
			current_bit = WriteHuffmanHeader(data, rep_map, current_bit, bytes);

			for(int64_t num : data) {
				auto& rep   = rep_map[num];
				current_bit = WriteNumUnsigned(rep.representation, rep.bit_size, current_bit, bytes);
			}

			return current_bit;

			/*
						struct CacheEntry {
							int64_t num { 0 };
							uint16_t entry_index { 0 };
							std::array<uint16_t, CACHE_ENTRY_SIZE> entries = { 0 };
						};
						static std::array<CacheEntry, CACHE_SIZE> cache;
						uint16_t cache_index = 0;

						int64_t last_num = 0;
						for(size_t i = 0; i < data.size(); i++) {
							int64_t num               = data[i];
							uint8_t default_size_bits = GetRequiredBits(i);

							// uint8_t cache_bits = 256;
							// if(cache[num % CACHE_SIZE] == num) {
							//	cache_bits = CACHE_BITS;
							//}

							CacheEntry* entry
								= std::find(cache.begin(), cache.end(), [&](CacheEntry& entry) { return entry.num ==
			   num; });

							if(entry == cache.end()) {
								entry      = &cache[cache_index];
								entry->num = num;
								entry->entries.fill(0);
								entry->entry_index = 0;
								if(cache_index == CACHE_SIZE - 1) {
									cache_index = 0;
								} else {
									cache_index++;
								}
							}

							uint16_t largest_match_location = 0;
							uint16_t largest_match_size     = 0;
							for(uint16_t loc : entry->entries) {
								uint16_t offset = 1;
								while(i + offset < data.size() && loc + offset < i) {
									if(data[loc + offset] != data[i + offset]) {
										break;
									}
									offset++;
								}

								if(offset > largest_match_size) {
									largest_match_location = loc;
									largest_match_size     = offset;
								}
							}

							if(largest_match_size > 2) {
								// Encode following size integers from sliding window
							}

							entry->entries[entry->entry_index] = i;
							if(entry->entry_index == CACHE_ENTRY_SIZE - 1) {
								entry->entry_index = 0;
							} else {
								entry->entry_index++;
							}

							last_num = num;
						}
						*/
		}

		uint64_t MoveBits(uint64_t start, uint64_t end, uint64_t new_start, std::vector<uint8_t>& bytes) {
			auto size = end - start;

			if(new_start > start) {
				if(bytes.size() <= ((new_start + size) >> 3)) {
					bytes.resize(((new_start + size) >> 3) + 1);
				}

				// Overengineered, was not faster
				/*
				uint64_t src  = start + size;
				uint64_t dest = new_start + size;

				if(src % 8 != 0) {
					uint8_t b = bytes[src >> 3] >> (8 - src % 8) & ((1UL << (src % 8)) - 1);

					if(dest % 8 == 0) {
						bytes[(dest >> 3) - 1] &= 0xFF << (src % 8);
						bytes[(dest >> 3) - 1] |= b;
					} else if(dest % 8 >= src % 8) {
						bytes[dest >> 3] &= ~(((1UL << (src % 8)) - 1) << (8 - dest % 8));
						bytes[dest >> 3] |= b << (8 - dest % 8);
					} else {
						bytes[dest >> 3] &= ((1UL << (8 - dest % 8)) - 1);
						bytes[dest >> 3] |= b << (8 - dest % 8);
						bytes[(dest >> 3) - 1] &= 0xFF << (src % 8 - dest % 8);
						bytes[(dest >> 3) - 1] |= b >> (dest % 8);
					}

					dest -= src % 8;
					src -= src % 8;
				}

				// Destination bitmasks
				const uint8_t dest_bits_right         = dest % 8;
				const uint8_t dest_mask_right_noshift = ((1UL << dest_bits_right) - 1);
				const uint8_t dest_mask_right         = dest_mask_right_noshift << (8 - dest_bits_right);
				const uint8_t dest_mask_left          = (1UL << (8 - dest_bits_right)) - 1;

				while(src - start != 0) {
					uint8_t b = bytes[(src >> 3) - 1];

					if(src - start > 8) {
						bytes[dest >> 3] &= dest_mask_left;
						bytes[dest >> 3] |= b << (8 - dest_bits_right);
						bytes[(dest >> 3) - 1] &= dest_mask_right;
						bytes[(dest >> 3) - 1] |= b >> dest_bits_right & dest_mask_left;
						src -= 8;
						dest -= 8;
					} else {
						if(dest % 8 == 0) {
							uint8_t mask = 0xFF << (src - start);
							bytes[(dest >> 3) - 1] &= mask;
							bytes[(dest >> 3) - 1] |= b & ((1UL << (src - start)) - 1);
						} else if(dest % 8 >= src - start) {
							uint8_t mask = ~(((1UL << (src - start)) - 1) << (8 - dest % 8));
							bytes[dest >> 3] &= mask;
							bytes[dest >> 3] |= (b & ((1UL << (src - start)) - 1)) << (8 - dest % 8);
						} else {
							bytes[dest >> 3] &= dest_mask_left;
							bytes[dest >> 3] |= b << (8 - dest_bits_right);
							bytes[(dest >> 3) - 1] &= 0xFF << (src - start - dest % 8);
							bytes[(dest >> 3) - 1] |= b >> (dest % 8) & ((1UL << (src - start - dest % 8)) - 1);
						}

						break;
					}
				}
				*/

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
	}
}