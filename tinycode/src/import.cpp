#include <tinycode.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <BarcodeFormat.h>
#include <BitMatrix.h>
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
			qrHints.setFormats(ZXing::BarcodeFormat::QRCode);

			ZXing::Result result
				= ZXing::ReadBarcode({ (uint8_t*)bitmap.pixmap().addr(), bitmap.width(), bitmap.height(), ZXing::ImageFormat::BGRX }, qrHints);

			if(!result.isValid()) {
				// Oh no
			}

			auto qr_bytes = result.bytes();
			std::copy(qr_bytes.begin(), qr_bytes.end(), std::back_inserter(bytes));
		}
	}
}