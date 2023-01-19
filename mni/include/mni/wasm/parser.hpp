#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mni {
	namespace Wasm {
		enum WasmItemType {
			NUM,           // Number of something
			SIZE,          // Size of something
			SECTION,       // Section id and size
			STRING,        // String of any kind
			TYPE,          // Type, if negative refers to value types
			INDEXED_TYPE,  // Type, only indexed
			LIMIT,         // Limit
			MEMORY_OP,     // For memory related operations
			INSTRUCTION,   // Instruction
			INSTRUCTION32, // Additional instructions
			ATTRIBUTE,     // Attribute / Mutability
			BREAK,         // Break offset, used in switch
			FUNCTION,      // Function index
			TABLE,         // Table index
			LOCAL,         // Local index
			GLOBAL,        // Global index
			MEMORY,        // Memory index, different than MEMORY_IDX which is u8
			TAG,           // Tag index
			I32,           // Literal
			I64,           // Literal
			I128,          // Literal
			F32,           // Literal
			F64,           // Literal
			ATOMIC_ORDER,  // Atomic order
			SEGMENT,       // Data segment
			MEMORY_IDX,    // Memory index, must be 0 in current version of wasm
			LANE,          // SIMD lane index
			STRUCT,        // Struct index
			EXTERNAL,      // Kind of external
			FLAGS,         // Used in some places
			DATA,          // Includes segments and user data
		};

		class Huffman {
		public:
			Huffman() { }

			// INSTRUCTION huffman
			bool INSTRUCTION_construct = false;
			std::unordered_map<uint8_t, Tree::Node<uint8_t>> INSTRUCTION_frequencies;
			bool INSTRUCTION_rep = false;
			std::unordered_map<uint8_t, Tree::NodeRepresentation> INSTRUCTION_rep_map;
			bool INSTRUCTION_tree                      = false;
			Mni::Tree::Node<uint8_t>* INSTRUCTION_root = new Mni::Tree::Node<uint8_t>();
			void INSTRUCTION_generate_rep() {
				GenerateHuffmanFrequencies(INSTRUCTION_frequencies, INSTRUCTION_rep_map);
				INSTRUCTION_rep = true;
			}
		};

		class IO {
		public:
			IO(std::vector<uint8_t>& bytes, Huffman& huffman)
				: bytes(bytes)
				, huffman(huffman) { }

			void WriteLEB(int64_t num);
			void WriteULEB(uint64_t num);
			int64_t ReadLEB();
			uint64_t ReadULEB();

			void WriteU8(uint8_t num);
			uint8_t ReadU8();

			void WriteU32(uint32_t num);
			uint32_t ReadU32();

			void WriteU64(uint64_t num);
			uint32_t ReadU64();

			void WriteFloat32(float num);
			float ReadFloat32();

			void WriteFloat64(double num);
			double ReadFloat64();

			bool Done();
			void Skip(size_t len);
			size_t GetPos();
			void Reset();

			void WriteSlice(std::vector<uint8_t> slice);
			void WriteString(std::string str);
			std::vector<uint8_t> ReadSlice(size_t len);
			std::string ReadString();

			Huffman& huffman;

		private:
			std::vector<uint8_t>& bytes;
			size_t i { 0 };
		};

		class OptimizedIO {
		public:
			OptimizedIO(std::vector<uint8_t>& bytes, uint64_t current_bit, Huffman& huffman)
				: bytes(bytes)
				, original_current_bit(current_bit)
				, current_bit(current_bit)
				, huffman(huffman) { }

			void WriteLEB(int64_t num);
			void WriteULEB(uint64_t num);
			int64_t ReadLEB();
			uint64_t ReadULEB();

			void WriteFloat32(float num);
			float ReadFloat32();

			void WriteFloat64(double num);
			double ReadFloat64();

			void WriteNum(int64_t num, uint8_t bit_size);
			int64_t ReadNum(uint8_t bit_size);

			void WriteUNum(uint64_t num, uint8_t bit_size);
			uint64_t ReadUNum(uint8_t bit_size);

			void WriteSlice(std::vector<uint8_t>& slice);
			void WriteString(std::string& str);
			std::vector<uint8_t> ReadSlice(size_t len);
			std::string ReadString(size_t len);

			void ReadSize() {
				size = ReadULEB();
				// Webassembly starts after size is read
				original_current_bit = current_bit;
			}
			void PrependSize();
			uint64_t GetSize() {
				return current_bit - original_current_bit;
			}
			bool Done() {
				return GetSize() == size;
			}

			uint64_t GetCurrentBit() {
				return current_bit;
			}
			void SetCurrentBit(uint64_t pos) {
				current_bit = pos;
			}

			template <typename T>
			void WriteHuffmanHeader(std::unordered_map<T, Mni::Tree::NodeRepresentation>& rep_map) {
				current_bit = Mni::Encoding::WriteHuffmanHeader(rep_map, current_bit, bytes);
			}

			template <typename T> void ReadHuffmanHeader(Mni::Tree::Node<T>* root) {
				current_bit = Mni::Decoding::ReadHuffmanHeader(root, current_bit, bytes);
			}

			template <typename T> void ReadHuffmanValue(Mni::Tree::Node<T>* root, T* num_out) {
				current_bit = Mni::Decoding::ReadHuffmanValue(root, num_out, current_bit, bytes);
			}

			Huffman& huffman;

		private:
			std::vector<uint8_t>& bytes;
			uint64_t original_current_bit;
			uint64_t current_bit;
			uint64_t size { 0 };
			uint8_t leb_multiple { 5 };
		};

		enum ParsingMode {
			READ_NORMAL,
			WRITE_NORMAL,
			READ_OPTIMIZED,
			WRITE_OPTIMIZED,
			NONE, // Used while finding values for huffman encoding
		};

		uint64_t NormalToOptimized(
			std::vector<uint8_t>& wasm_bytes, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t OptimizedToNormal(
			std::vector<uint8_t>& wasm_bytes, uint64_t current_bit, std::vector<uint8_t>& bytes);
	}
}