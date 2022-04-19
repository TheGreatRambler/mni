#pragma once

#include <tinycode/tree.hpp>

#include <cstdint>
#include <vector>

namespace TinyCode {
	namespace Instructions {
		class Instruction {
		public:
			Instruction();
			virtual bool HandleBit(uint8_t bit);
			virtual std::string GetString();
		};

		class Variable : Instruction {
		public:
			bool HandleBit(uint8_t bit);
			std::string GetString();

		private:
			enum State : uint8_t {
				NAME,
				SIZE,
				IS_TAGGED,
				TAG,
				VALUE,
				ENUM_END,
			};

			enum DataSize : uint8_t {};

			State state { State::NAME };
			uint8_t current_bit { 0 };
			uint64_t number { 0 };
			uint8_t counter { 0 };

			uint8_t variable_name;
			uint8_t variable_size;
			int64_t variable_value;
		};
	}

	namespace Encoding {
		void FixLastByte(uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint8_t GetRequiredBits(int64_t num);

		uint64_t WriteNum(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t WriteNumUnsigned(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t WriteTaggedNum(int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t WriteTaggedNumUnsigned(
			int64_t num, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);

		enum IntegerListEncodingType : uint8_t {
			FIXED        = 0,
			TAGGED       = 1,
			DELTA_FIXED  = 2,
			DELTA_TAGGED = 3,
			ENUM_END,
		};

		uint64_t WriteIdealIntegerList(std::vector<int64_t> data, uint64_t current_bit, std::vector<uint8_t>& bytes);

		static constexpr uint8_t LIST_TYPE_BITS = 2;
		static constexpr uint8_t LIST_SIZE_BITS = 16;
	}

	namespace Decoding {
		uint64_t ReadNum(int64_t* num_out, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadNumUnsigned(int64_t* num_out, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadTaggedNum(int64_t* num_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadTaggedNumUnsigned(int64_t* num_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadIdealIntegerList(
			std::vector<int64_t>& data_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
	}

	namespace Debug {
		std::string Print(uint64_t size, std::vector<uint8_t>& bytes, bool with_delimiter);
	}

	class Parser {
	public:
		Parser(uint8_t* buf, size_t size);
		std::vector<std::string> GetInstructionStrings();

	private:
		std::vector<std::string> instructions;
	};
}