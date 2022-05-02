#include <tinycode.hpp>

namespace TinyCode {
	namespace Instructions {
		bool Variable::HandleBit(uint8_t bit) {
			switch(state) {
			case State::NAME: {
				if(counter == 0) {
					// Maximum bitsize of variable name, allows for 256 names, will be configurable later
					counter = 8;
				}

				number = (number << 1) | bit;
				counter--;

				if(counter == 0) {
					variable_name = number;
					number        = 0;
					state         = State::SIZE;
				}

				current_bit++;
			} break;
			case State::SIZE: {
				// TODO use binary tree
			} break;
			}
		}
	}
}