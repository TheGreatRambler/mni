#pragma once

#include <tinycode/tree.hpp>

#include <cmath>
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
		void CopyOver(std::vector<uint8_t>& src, uint64_t size, uint64_t src_offset, std::vector<uint8_t>& dest,
			uint64_t dest_offset);
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
		};

		enum IntegerListCompressionType : uint8_t {
			NONE        = 0,
			LOOK_BEHIND = 1,
			LOOKUP      = 2,
			CHANGE_SIZE = 3,
			DELTA       = 4,
			HUFFMAN     = 5,
		};

		uint64_t WriteHuffmanHeader(std::vector<int64_t> data,
			std::unordered_map<int64_t, TinyCode::Tree::NodeRepresentation>& rep_map, uint64_t current_bit,
			std::vector<uint8_t>& bytes);

		// Cache hashing is modulo 2^(cache bits)

		uint64_t WriteSimpleIntegerList(std::vector<int64_t> data, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t WriteHuffmanIntegerList(std::vector<int64_t> data, uint64_t current_bit, std::vector<uint8_t>& bytes);

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

	namespace Decoding {
		uint64_t ReadNum(int64_t* num_out, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadNumUnsigned(int64_t* num_out, uint8_t bit_size, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadTaggedNum(int64_t* num_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadTaggedNumUnsigned(int64_t* num_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t Read1Bit(bool* bit_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadHuffmanHeader(TinyCode::Tree::Node* root, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadHuffmanList(TinyCode::Tree::Node* root, std::vector<int64_t>& data_out, int64_t data_size,
			uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadSimpleIntegerList(
			std::vector<int64_t>& data_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t ReadHuffmanIntegerList(
			std::vector<int64_t>& data_out, uint64_t current_bit, std::vector<uint8_t>& bytes);
	}

	namespace Debug {
		std::string Print(uint64_t size, std::vector<uint8_t>& bytes, bool with_delimiter);
		std::string PrintAsCArray(uint64_t size, std::vector<uint8_t>& bytes, bool with_delimiter);
		bool AreIdentical(std::vector<uint8_t>& bytes1, std::vector<uint8_t>& bytes2, uint64_t size);
	}

	class Parser {
	public:
		Parser(uint8_t* buf, size_t size);
		std::vector<std::string> GetInstructionStrings();

	private:
		std::vector<std::string> instructions;
	};

	namespace Export {
		void GenerateQRCode(uint64_t size, std::vector<uint8_t>& bytes, int width, int height, std::string path);
	}

	namespace Import {
		void ScanQRCode(std::vector<uint8_t>& bytes, std::string path);
	}
}