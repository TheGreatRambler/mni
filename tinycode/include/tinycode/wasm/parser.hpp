#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace TinyCode {
	namespace Wasm {
		class IO {
		public:
			IO(std::vector<uint8_t>& bytes)
				: bytes(bytes) { }

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

			void WriteSlice(std::vector<uint8_t> slice);
			void WriteString(std::string str);
			std::vector<uint8_t> ReadSlice(size_t len);
			std::string ReadString();

		private:
			std::vector<uint8_t>& bytes;
			size_t i = 0;
		};

		class OptimizedIO {
		public:
			OptimizedIO(std::vector<uint8_t>& bytes, uint64_t current_bit)
				: bytes(bytes)
				, original_current_bit(current_bit)
				, current_bit(current_bit) { }

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

		private:
			std::vector<uint8_t>& bytes;
			uint64_t original_current_bit;
			uint64_t current_bit;
			uint64_t size        = 0;
			uint8_t leb_multiple = 5;
		};

		enum ParsingMode {
			READ_NORMAL,
			WRITE_NORMAL,
			READ_OPTIMIZED,
			WRITE_OPTIMIZED,
		};

		uint64_t ConvertWasm(std::vector<uint8_t>& wasm_bytes, uint64_t current_bit, std::vector<uint8_t>& bytes, ParsingMode in, ParsingMode out);
		uint64_t NormalToOptimized(std::vector<uint8_t>& wasm_bytes, uint64_t current_bit, std::vector<uint8_t>& bytes);
		uint64_t OptimizedToNormal(std::vector<uint8_t>& wasm_bytes, uint64_t current_bit, std::vector<uint8_t>& bytes);
	}
}