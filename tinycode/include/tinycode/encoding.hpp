#pragma once

#include <tinycode/tree.hpp>

#include <bit>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace TinyCode {
	namespace Encoding {
		struct DataHeader {
			uint16_t size;
			// TODO consider adding type
		};

		static constexpr uint8_t DEFAULT_LEB_MULTIPLE = 7;

		uint64_t AddDataHeader(uint64_t current_bit, std::vector<uint8_t>& bytes, DataHeader header);
		void CopyOverSrcOffset(std::vector<uint8_t>& src, uint64_t size, uint64_t src_offset, std::vector<uint8_t>& dest);
		void CopyOverDestOffset(std::vector<uint8_t>& src, uint64_t size, std::vector<uint8_t>& dest, uint64_t dest_offset);

		template <typename T> uint8_t GetRequiredBits(T num) {
			static_assert(std::is_integral<T>::value, "Must be passed integral type");
			if constexpr(std::is_signed<T>::value) {
				typedef typename std::make_unsigned<T>::type unsigned_t;
				return std::bit_width((unsigned_t)std::abs(num));
			} else {
				return std::bit_width(num);
			}
		}

		template <typename T> uint8_t GetRequiredLEBBits(T num, uint8_t multiple_bits) {
			uint8_t required_bits = GetRequiredBits(num);
			return std::ceil(required_bits / (float)multiple_bits) * (multiple_bits + 1);
		}

		template <typename T> uint64_t WriteNum(T num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			static_assert(std::is_integral<T>::value, "Must be passed integral type");
			if constexpr(std::is_signed<T>::value) {
				current_bit = Write1Bit(num < 0, current_bit, bytes);
				return WriteNumUnsigned(std::abs(num), bit_size, current_bit, bytes);
			} else {
				current_bit = Write1Bit(false, current_bit, bytes);
				return WriteNumUnsigned(num, bit_size, current_bit, bytes);
			}
		}

		template <typename T> uint64_t WriteNumUnsigned(T num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			static_assert(std::is_integral<T>::value, "Must be passed integral type");

			if constexpr(std::is_signed<T>::value) {
				num = std::abs(num);
			}

			constexpr uint8_t type_size = sizeof(T) * 8;
			T num_to_write              = num << (type_size - bit_size);
			while(bit_size != 0) {
				current_bit = Write1Bit(num_to_write & (1ULL << (type_size - 1)), current_bit, bytes);
				num_to_write <<= 1;
				bit_size--;
			}

			return current_bit;
		}

		template <typename T> uint64_t WriteTaggedNum(T num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			static_assert(std::is_integral<T>::value, "Must be passed integral type");
			current_bit = WriteNumUnsigned(bit_size, 6, current_bit, bytes);
			return WriteNum(num, bit_size, current_bit, bytes);
		}

		template <typename T> uint64_t WriteTaggedNumUnsigned(T num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			static_assert(std::is_integral<T>::value, "Must be passed integral type");
			current_bit = WriteNumUnsigned(bit_size, 6, current_bit, bytes);
			return WriteNumUnsigned(num, bit_size, current_bit, bytes);
		}

		template <typename T> uint64_t WriteLEB(T num, uint8_t multiple_bits, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			static_assert(std::is_integral<T>::value, "Must be passed integral type");
			current_bit = Write1Bit(num < 0, current_bit, bytes);

			if constexpr(std::is_signed<T>::value) {
				num = std::abs(num);
			}

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

		template <typename T> uint64_t WriteLEBUnsigned(T num, uint8_t multiple_bits, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			static_assert(std::is_integral<T>::value, "Must be passed integral type");

			if constexpr(std::is_signed<T>::value) {
				num = std::abs(num);
			}

			int8_t required_bits = GetRequiredBits(num);
			while(required_bits >= 0) {
				const uint64_t mask = (1UL << multiple_bits) - 1;
				current_bit         = WriteNumUnsigned(num & mask, multiple_bits, current_bit, bytes);
				num >>= multiple_bits;
				required_bits -= multiple_bits;

				current_bit = Write1Bit(required_bits <= 0, current_bit, bytes);
			}
			return current_bit;
		}

		uint64_t Write1Bit(bool bit, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t WriteFloat(float num, uint8_t mantissa_bits_to_remove, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t WriteDouble(double num, uint8_t mantissa_bits_to_remove, uint64_t current_bit, std::vector<uint8_t>& bytes);

		enum IntegerListEncodingType : uint8_t {
			FIXED        = 0,
			TAGGED       = 1,
			DELTA_FIXED  = 2,
			DELTA_TAGGED = 3,
		};

		enum IntegerListCompressionType : uint8_t {
			NONE        = 0,
			LOOK_BEHIND = 1,
			LOOKUP      = 2,
			CHANGE_SIZE = 3,
			DELTA       = 4,
			HUFFMAN     = 5,
		};

		template <typename T>
		uint64_t WriteHuffmanHeader(std::vector<T> data, std::unordered_map<T, TinyCode::Tree::NodeRepresentation>& rep_map, uint64_t current_bit,
			std::vector<uint8_t>& bytes) {
			TinyCode::Tree::GenerateHuffman(data, rep_map);
			return WriteHuffmanHeader(rep_map, current_bit, bytes);
		}

		template <typename T>
		uint64_t WriteHuffmanHeader(
			std::unordered_map<T, TinyCode::Tree::NodeRepresentation>& rep_map, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			std::vector<T> element_list;
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

		template <typename T> uint64_t WriteLEBIntegerList(std::vector<T> data, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			bool every_element_positive = true;
			for(auto num : data) {
				if(num < 0) {
					every_element_positive = false;
					break;
				}
			}

			// List size
			current_bit = WriteLEBUnsigned(data.size(), DEFAULT_LEB_MULTIPLE, current_bit, bytes);
			// Whether every element is positive
			current_bit = Write1Bit(every_element_positive, current_bit, bytes);

			for(auto num : data) {
				if(every_element_positive) {
					current_bit = WriteLEBUnsigned(num, DEFAULT_LEB_MULTIPLE, current_bit, bytes);
				} else {
					current_bit = WriteLEB(num, DEFAULT_LEB_MULTIPLE, current_bit, bytes);
				}
			}

			return current_bit;
		}

		// Simple works extremely well for tiny arrays, but not for general compression
		template <typename T> uint64_t WriteSimpleIntegerList(std::vector<T> data, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			if(data.size() > (0x1 << LIST_SIZE_BITS)) {
				// TODO data array is too large for chosen list size bits, ask user to compile under different settings
			}

			// Determine best encoding scheme
			uint8_t max_bits_required_fixed           = 0;
			uint64_t total_bits_required_tagged       = LIST_TYPE_BITS + LIST_SIZE_BITS + 1;
			uint8_t max_bits_required_delta_fixed     = 0;
			uint64_t total_bits_required_delta_tagged = LIST_TYPE_BITS + LIST_SIZE_BITS + 1;
			T last_num                                = 0;
			bool every_element_positive               = true;
			bool every_element_positive_delta         = true;
			for(auto num : data) {
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

			uint64_t total_bits_required_fixed       = LIST_TYPE_BITS + LIST_SIZE_BITS + 1 + 6 + data.size() * max_bits_required_fixed;
			uint64_t total_bits_required_delta_fixed = LIST_TYPE_BITS + LIST_SIZE_BITS + 1 + 6 + data.size() * max_bits_required_delta_fixed;

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
				current_bit = WriteNumUnsigned((uint8_t)IntegerListEncodingType::FIXED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = Write1Bit(every_element_positive, current_bit, bytes);
				// Bits used for each number in list
				current_bit = WriteNumUnsigned(max_bits_required_fixed, 6, current_bit, bytes);

				for(auto num : data) {
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
				current_bit = WriteNumUnsigned((uint8_t)IntegerListEncodingType::TAGGED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = Write1Bit(every_element_positive, current_bit, bytes);

				for(auto num : data) {
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
				current_bit = WriteNumUnsigned((uint8_t)IntegerListEncodingType::DELTA_FIXED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = Write1Bit(every_element_positive_delta, current_bit, bytes);
				// Bits used for each number in list
				current_bit = WriteNumUnsigned(max_bits_required_delta_fixed, 6, current_bit, bytes);

				T last_num = 0;
				for(auto num : data) {
					// Write number using bits specified earlier
					if(every_element_positive_delta) {
						current_bit = WriteNumUnsigned(num - last_num, max_bits_required_delta_fixed, current_bit, bytes);
					} else {
						current_bit = WriteNum(num - last_num, max_bits_required_delta_fixed, current_bit, bytes);
					}
					last_num = num;
				}
			} else if(min_bits == total_bits_required_delta_tagged) {
				// std::cout << "Chosen delta tagged with " << (int)min_bits << std::endl;
				//  List type
				current_bit = WriteNumUnsigned((uint8_t)IntegerListEncodingType::DELTA_TAGGED, LIST_TYPE_BITS, current_bit, bytes);
				// List size
				current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);
				// Whether every element is positive
				current_bit = Write1Bit(every_element_positive_delta, current_bit, bytes);

				T last_num = 0;
				for(auto num : data) {
					auto num_delta        = num - last_num;
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

		template <typename T> uint64_t WriteHuffmanIntegerList(std::vector<T> data, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			if(data.size() > (0x1 << LIST_SIZE_BITS)) {
				// TODO data array is too large for chosen list size bits, ask user to compile under different settings
			}

			// List size
			current_bit = WriteNumUnsigned(data.size(), LIST_SIZE_BITS, current_bit, bytes);

			std::unordered_map<T, TinyCode::Tree::NodeRepresentation> rep_map;
			current_bit = WriteHuffmanHeader(data, rep_map, current_bit, bytes);

			for(int64_t num : data) {
				auto& rep   = rep_map[num];
				current_bit = WriteNumUnsigned(rep.representation, rep.bit_size, current_bit, bytes);
			}

			return current_bit;
		}

		uint64_t MoveBits(uint64_t start, uint64_t end, uint64_t new_start, std::vector<uint8_t>& bytes);
		uint64_t CopyBits(uint64_t start, uint64_t end, uint64_t new_start, std::vector<uint8_t>& bytes_src, std::vector<uint8_t>& bytes_dest);

		// Handles 4 different encoding types
		static constexpr uint8_t LIST_TYPE_BITS = 2;
		// Handles up to 16777216 element vectors
		static constexpr uint8_t LIST_SIZE_BITS = 24;
		// Compression type in stream, handles 4 types
		static constexpr uint8_t COMPRESSION_TYPE_BITS = 3;
		// Bits used for delta, determines how large the delta can be before delta is not used
		static constexpr uint8_t DELTA_BITS = 3;
		static constexpr uint8_t DELTA_SIZE = 0x1 << DELTA_BITS;
		// 16 possible numbers in cache at any given time
		static constexpr uint8_t CACHE_BITS = 4;
		static constexpr uint8_t CACHE_SIZE = 0x1 << CACHE_BITS;
		// 8 possible indices for each number
		static constexpr uint8_t CACHE_ENTRY_BITS = 3;
		static constexpr uint8_t CACHE_ENTRY_SIZE = 0x1 << CACHE_ENTRY_BITS;
	}
}