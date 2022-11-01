#include <mni.hpp>

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
#include <TextUtfEncoding.h>
#include <core/SkBitmap.h>
#include <core/SkCanvas.h>
#include <core/SkColorSpace.h>
#include <core/SkGraphics.h>
#include <core/SkImage.h>
#include <core/SkImageEncoder.h>
#include <core/SkImageGenerator.h>
#include <core/SkString.h>
#include <encode/SkPngEncoder.h>

namespace Mni {
	namespace Import {
		void ScanQRCode(std::vector<uint8_t>& bytes, std::string path) {
			auto gen = SkImageGenerator::MakeFromEncoded(SkData::MakeFromFileName(path.c_str()));

			SkBitmap bitmap;
			bitmap.allocPixels(gen->getInfo());
			// Always generates BGRA
			gen->getPixels(
				gen->getInfo().makeColorSpace(nullptr), bitmap.getPixels(), bitmap.rowBytes());

			ZXing::DecodeHints qrHints;
			qrHints.setFormats(ZXing::BarcodeFormat::QRCode);

			ZXing::Result result
				= ZXing::ReadBarcode({ (uint8_t*)bitmap.pixmap().addr(), bitmap.width(),
										 bitmap.height(), ZXing::ImageFormat::BGRX },
					qrHints);

			if(!result.isValid()) {
				// Oh no
			}

			auto qr_bytes = result.bytes();
			std::copy(qr_bytes.begin(), qr_bytes.end(), std::back_inserter(bytes));
		}
	}
}