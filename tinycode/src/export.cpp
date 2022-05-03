#include <tinycode.hpp>

#include <cstdint>
#include <string>

#include <BarcodeFormat.h>
#include <BitMatrix.h>
#include <CharacterSetECI.h>
#include <MultiFormatWriter.h>
#include <SkBitmap.h>
#include <SkCanvas.h>
#include <SkGraphics.h>
#include <SkImage.h>
#include <SkImageEncoder.h>
#include <SkPngEncoder.h>
#include <SkString.h>
#include <TextUtfEncoding.h>
#include <iostream>
#include <qrcode/QRErrorCorrectionLevel.h>
#include <qrcode/QRWriter.h>

namespace TinyCode {
	namespace Export {
		void GenerateQRCode(uint64_t size, std::vector<uint8_t>& bytes, int width, int height, std::string path) {
			ZXing::QRCode::Writer writer;
			writer.setMargin(1);
			writer.setEncoding(ZXing::CharacterSet::Unknown);
			writer.setErrorCorrectionLevel(ZXing::QRCode::ErrorCorrectionLevel::Low);

			std::wstring output(bytes.begin(), bytes.end());
			auto matrix = writer.encode(output, 1, 1);

			constexpr int pixel_size = 10;

			SkBitmap bitmap;
			bitmap.allocPixels(SkImageInfo::Make(matrix.height() * pixel_size, matrix.width() * pixel_size,
								   SkColorType::kRGB_888x_SkColorType, SkAlphaType::kOpaque_SkAlphaType),
				0);

			SkCanvas canvas(bitmap);
			canvas.clear(SkColors::kWhite);

			for(int y = 0; y < matrix.height(); y++) {
				for(int x = 0; x < matrix.width(); x++) {
					if(matrix.get(x, y)) {
						canvas.drawRect(SkRect::MakeXYWH(x * pixel_size, y * pixel_size, pixel_size, pixel_size),
							SkPaint(SkColors::kBlack));
					}
				}
			}

			// SkPixmap src;
			// bool success = bitmap.peekPixels(&src);
			// SkDynamicMemoryWStream dest;
			// SkPngEncoder::Options options;
			// success = SkPngEncoder::Encode(&dest, src, options);

			SkPixmap src;
			bool success = bitmap.peekPixels(&src);
			SkFILEWStream dest(path.c_str());
			SkPngEncoder::Options options;
			options.fZLibLevel = 5;
			success            = SkPngEncoder::Encode(&dest, src, options);
		}
	}
}