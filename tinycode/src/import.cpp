#include <tinycode.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <BarcodeFormat.h>
#include <BitMatrix.h>
#include <CharacterSetECI.h>
#include <MultiFormatWriter.h>
#include <ReadBarcode.h>
#include <SkBitmap.h>
#include <SkCanvas.h>
#include <SkColorSpace.h>
#include <SkGraphics.h>
#include <SkImage.h>
#include <SkImageEncoder.h>
#include <SkImageGenerator.h>
#include <SkPngEncoder.h>
#include <SkString.h>
#include <TextUtfEncoding.h>

namespace TinyCode {
	namespace Import {
		void ScanQRCode(std::vector<uint8_t>& bytes, std::string path) {
			auto gen = SkImageGenerator::MakeFromEncoded(SkData::MakeFromFileName(path.c_str()));

			SkBitmap bitmap;
			bitmap.allocPixels(gen->getInfo());
			// Always generates BGRA
			gen->getPixels(gen->getInfo().makeColorSpace(nullptr), bitmap.getPixels(), bitmap.rowBytes());

			ZXing::DecodeHints qrHints;
			qrHints.setFormats(ZXing::BarcodeFormat::QR_CODE);

			ZXing::Result result = ZXing::ReadBarcode(
				{ (uint8_t*)bitmap.pixmap().addr(), bitmap.width(), bitmap.height(), ZXing::ImageFormat::BGRX },
				qrHints);
			ZXing::DecodeStatus qrStatus = result.status();

			if(qrStatus != ZXing::DecodeStatus::NoError) {
				// Oh no
			}

			auto raw_bytes = result.rawBytes();
			// Output from QR code is shifted by 20 bits
			TinyCode::Encoding::CopyOver(raw_bytes, result.numBits() - 24, 20, bytes, 0);
		}
	}
}