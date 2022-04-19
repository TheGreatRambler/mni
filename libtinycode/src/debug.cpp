#include <tinycode.hpp>

#include <bitset>
#include <iostream>

namespace TinyCode {
	namespace Debug {
		std::string Print(uint64_t size, std::vector<uint8_t>& bytes, bool with_delimiter) {
			std::string output;
			for(uint64_t i = 0; i < bytes.size(); i++) {
				uint64_t remainder = size - i * 8;
				if(remainder < 8) {
					output += std::bitset<CHAR_BIT>(bytes[i]).to_string().substr(0, remainder);
				} else {
					if(with_delimiter) {
						output += std::bitset<CHAR_BIT>(bytes[i]).to_string() + " ";
					} else {
						output += std::bitset<CHAR_BIT>(bytes[i]).to_string();
					}
				}
			}
			return output;
		}
	}
}