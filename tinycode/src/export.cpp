#include <tinycode.hpp>

#include <cstdint>
#include <string>

#include <BarcodeFormat.h>
#include <BitMatrix.h>
#include <CharacterSetECI.h>
#include <MultiFormatWriter.h>
#include <TextUtfEncoding.h>

namespace TinyCode {
	namespace Export {
		void GenerateQRCode(uint64_t size, std::vector<uint8_t>& bytes, int width, int height) {
			auto writer = ZXing::MultiFormatWriter(ZXing::BarcodeFormat::QRCode)
							  .setMargin(10)
							  .setEncoding(ZXing::CharacterSet::BINARY)
							  .setEccLevel(1);
			std::wstring output;
			ZXing::TextUtfEncoding::AppendUtf8(output, bytes.data(), std::ceil(size / 8.0));
			auto bitmap = ZXing::ToMatrix<uint8_t>(writer.encode(output, width, height));
			// TODO figure out how to export to user
		}
	}
}