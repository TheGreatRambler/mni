#pragma once

#include <cstdint>
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
			uint8_t PeekU8();
			uint8_t LastU8();

			void WriteU32(uint32_t num);
			uint32_t ReadU32();

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

		uint64_t WasmToOptimized(std::vector<uint8_t> wasm_bytes, uint64_t current_bit, std::vector<uint8_t> bytes);
	}
}