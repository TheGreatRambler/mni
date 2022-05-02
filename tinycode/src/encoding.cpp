#include <tinycode.hpp>
#include <tinycode/tree.hpp>

#include <bit>
#include <bitset>
#include <cstdint>
#include <iostream>
#include <unordered_map>

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
			// Must cast to unsigned but works regardless
			return std::bit_width<uint64_t>(std::abs(num));
		}

		uint64_t WriteNum(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t num_to_write = std::abs(num) << (64 - bit_size);
			bool sign_set        = false;
			while(true) {
				if(bit_size == 0) {
					return current_bit;
				}

				if(current_bit % 8 == 0) {
					if(bytes.size() <= (current_bit >> 3)) {
						bytes.push_back(0);
					}
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
					if(bytes.size() <= (current_bit >> 3)) {
						bytes.push_back(0);
					}
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

		uint64_t WriteHuffmanHeader(std::vector<int64_t> data,
			std::unordered_map<int64_t, TinyCode::Tree::NodeRepresentation>& rep_map, uint64_t current_bit,
			std::vector<uint8_t>& bytes) {
			std::unordered_map<int64_t, TinyCode::Tree::Node> element_frequencies;

			for(int64_t num : data) {
				if(element_frequencies.count(num)) {
					element_frequencies[num].freq++;
				} else {
					element_frequencies[num] = TinyCode::Tree::Node(num, 1);
				}
			}

			std::vector<TinyCode::Tree::Node> element_frequencies_list;

			for(auto& element : element_frequencies) {
				element_frequencies_list.push_back(element.second);
			}

			TinyCode::Tree::Node* root = TinyCode::Tree::BuildHuffman(element_frequencies_list);
			// TinyCode::Tree::PrintTree<char>(root, "");
			TinyCode::Tree::BuildRepresentation(root, rep_map);
			TinyCode::Tree::FreeTree(root);

			std::vector<int64_t> element_list;
			std::vector<TinyCode::Tree::NodeRepresentation> representation_list;
			for(auto& element : rep_map) {
				// std::cout << (char)element.first << " has representation bit size "
				//		  << (int)element.second->representation.bit_size << std::endl;
				element_list.push_back(element.first);
				representation_list.push_back(element.second);
			}

			current_bit = WriteSimpleIntegerList(element_list, current_bit, bytes);

			for(int i = 0; i < element_list.size(); i++) {
				auto& rep = representation_list[i];
				// std::string rep_string = std::bitset<64>(rep.representation).to_string();
				// std::cout << (char)element_list[i] << ": " << rep_string.substr(rep_string.size() - rep.bit_size)
				//		  << std::endl;
				current_bit = WriteNumUnsigned(rep.bit_size, 6, current_bit, bytes);
				current_bit = WriteNumUnsigned(rep.representation, rep.bit_size, current_bit, bytes);
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
	}
}