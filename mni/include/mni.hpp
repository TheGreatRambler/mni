#pragma once

#include <mni/decoding.hpp>
#include <mni/encoding.hpp>
#include <mni/tree.hpp>
#include <mni/wasm/parser.hpp>
#include <mni/wasm/runtime.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace Mni {
	namespace Debug {
		std::string Print(uint64_t size, std::vector<uint8_t>& bytes, bool with_delimiter);
		std::string PrintAsCArray(uint64_t size, std::vector<uint8_t>& bytes);
		std::string PrintAsCArray(std::vector<uint8_t>& bytes);
		bool AreIdentical(
			std::vector<uint8_t>& bytes1, std::vector<uint8_t>& bytes2, uint64_t size);
	}

	class Parser {
	public:
		Parser(uint8_t* buf, size_t size);
		std::vector<std::string> GetInstructionStrings();

	private:
		std::vector<std::string> instructions;
	};

	namespace Wasm {
		void RemoveUnneccesary(std::vector<uint8_t>& in, std::vector<uint8_t>& out,
			std::unordered_map<int, std::string> kept_names);
		void GetExports(std::vector<uint8_t>& in, std::vector<std::string>& names);
	}

	namespace Export {
		bool GenerateQRCode(
			uint64_t size, std::vector<uint8_t>& bytes, int width, int height, std::string path);
	}

	namespace Import {
		void ScanQRCode(std::vector<uint8_t>& bytes, std::string path);
	}
}