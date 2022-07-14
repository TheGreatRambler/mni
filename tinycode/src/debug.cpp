#include <tinycode.hpp>

#include <bitset>
#include <iostream>

namespace TinyCode {
	namespace Debug {
		std::string Print(uint64_t size, std::vector<uint8_t>& bytes, bool with_delimiter) {
			std::string output;
			for(uint64_t i = 0; i < bytes.size(); i++) {
				uint64_t remainder = size - i * 8;
				if(remainder <= 8) {
					output += std::bitset<CHAR_BIT>(bytes[i]).to_string().substr(0, remainder);
					return output;
				} else {
					if(with_delimiter) {
						output += std::bitset<CHAR_BIT>(bytes[i]).to_string() + " ";
					} else {
						output += std::bitset<CHAR_BIT>(bytes[i]).to_string();
					}
				}
			}
		}

		std::string PrintAsCArray(uint64_t size, std::vector<uint8_t>& bytes) {
			constexpr char hexmap[]
				= { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
			uint64_t current_size = 0;
			std::string output    = "{";
			for(uint8_t byte : bytes) {
				if(current_size >= size)
					break;

				output += "0x";
				output += hexmap[(byte & 0xF0) >> 4];
				output += hexmap[byte & 0x0F];
				output += ",";
				current_size += 8;
			}
			output.pop_back();
			output += "}";
			return output;
		}

		std::string PrintAsCArray(std::vector<uint8_t>& bytes) {
			return PrintAsCArray(bytes.size() * 8, bytes);
		}

		bool AreIdentical(std::vector<uint8_t>& bytes1, std::vector<uint8_t>& bytes2, uint64_t size) {
			for(int i = 0; i < (size >> 3); i++) {
				if(bytes1[i] != bytes2[i]) {
					return false;
				}
			}
			return true;
		}
	}
}