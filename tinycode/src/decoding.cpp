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

		uint64_t Read1Bit(bool* bit_out, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			*bit_out = bytes[current_bit >> 3] << (current_bit % 8) & 0b10000000;
			return current_bit + 1;
		}

		uint64_t ReadHuffmanHeader(TinyCode::Tree::Node* root, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			std::vector<int64_t> elements;
			current_bit = ReadSimpleIntegerList(elements, current_bit, bytes);

			for(int i = 0; i < elements.size(); i++) {
				int64_t representation;
				int64_t bit_size;
				current_bit = ReadNumUnsigned(&bit_size, 6, current_bit, bytes);
				current_bit = ReadNumUnsigned(&representation, (uint8_t)bit_size, current_bit, bytes);

				TinyCode::Tree::Node* current_root = root;
				for(int8_t bit = bit_size - 1; bit > -1; bit--) {
					if(representation & (0x1 << bit)) {
						if(!current_root->right) {
							current_root->right = new TinyCode::Tree::Node();
						}
						current_root = current_root->right;
					} else {
						if(!current_root->left) {
							current_root->left = new TinyCode::Tree::Node();
						}
						current_root = current_root->left;
					}

					if(bit == 0) {
						current_root->data = elements[i];
					}
				}
			}

			return current_bit;
		}

		uint64_t ReadHuffmanList(TinyCode::Tree::Node* root, std::vector<int64_t>& data_out, int64_t data_size,
			uint64_t current_bit, std::vector<uint8_t>& bytes) {
			TinyCode::Tree::Node* current_root = root;
			int64_t read_integers              = 0;
			while(read_integers != data_size) {
				if(current_root->left == NULL && current_root->right == NULL) {
					// Leaf with data
					data_out.push_back(current_root->data);
					read_integers++;
					current_root = root;
				} else {
					// Still reading path
					bool direction;
					current_bit = Read1Bit(&direction, current_bit, bytes);
					if(direction) {
						current_root = current_root->right;
					} else {
						current_root = current_root->left;
					}
				}
			}

			return current_bit;
		}

		uint64_t ReadSimpleIntegerList(
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

		uint64_t ReadHuffmanIntegerList(
			std::vector<int64_t>& data_out, uint64_t current_bit, std::vector<uint8_t>& bytes) {
			int64_t list_size;
			current_bit = ReadNumUnsigned(&list_size, Encoding::LIST_SIZE_BITS, current_bit, bytes);

			TinyCode::Tree::Node* root = new TinyCode::Tree::Node();
			current_bit                = ReadHuffmanHeader(root, current_bit, bytes);
			current_bit                = ReadHuffmanList(root, data_out, list_size, current_bit, bytes);

			return current_bit;
		}
	}
}