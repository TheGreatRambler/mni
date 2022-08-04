#include <tinycode.hpp>

#include <cstdint>
#include <string>

#include <BarcodeFormat.h>
#include <BitMatrix.h>
#include <CharacterSetECI.h>
#include <MultiFormatWriter.h>
#include <SkBitmap.h>
#include <SkCanvas.h>
#include <SkFont.h>
#include <SkGraphics.h>
#include <SkImage.h>
#include <SkImageEncoder.h>
#include <SkPngEncoder.h>
#include <SkTextBlob.h>
#include <SkTypeface.h>
#include <TextUtfEncoding.h>
#include <iostream>
#include <qrcode/QRErrorCorrectionLevel.h>
#include <qrcode/QRWriter.h>

namespace TinyCode {
	namespace Export {
		bool GenerateQRCode(uint64_t size, std::vector<uint8_t>& bytes, int width, int height, std::string path) {
			constexpr int pixel_size    = 10;
			constexpr int margin_size   = 3;
			constexpr int bottom_margin = 0; // 200

			if(bytes.size() > 2953)
				return false;

			ZXing::QRCode::Writer writer;
			writer.setMargin(margin_size);
			writer.setEncoding(ZXing::CharacterSet::BINARY);
			writer.setErrorCorrectionLevel(ZXing::QRCode::ErrorCorrectionLevel::Low);

			std::wstring output(bytes.begin(), bytes.end());
			auto matrix = writer.encode(output, 1, 1);

			if(matrix.empty())
				return false;

			SkBitmap bitmap;
			bitmap.allocPixels(SkImageInfo::Make(matrix.width() * pixel_size, matrix.height() * pixel_size + bottom_margin,
								   SkColorType::kRGB_888x_SkColorType, SkAlphaType::kOpaque_SkAlphaType),
				0);

			SkCanvas canvas(bitmap);
			canvas.clear(SkColors::kWhite);

			SkPaint pixel_paint;
			pixel_paint.setColor(SkColorSetRGB(128, 0, 0));

			for(int y = 0; y < matrix.height(); y++) {
				for(int x = 0; x < matrix.width(); x++) {
					if(matrix.get(x, y)) {
						canvas.drawRect(SkRect::MakeXYWH(x * pixel_size, y * pixel_size, pixel_size, pixel_size), pixel_paint);
					}
				}
			}

			/*
						// SkTypeface::MakeFromFile("/skimages/samplefont.ttf")
						SkFont text_font(SkTypeface::MakeFromFile("../Hack-Bold.ttf"));
						text_font.setSize(pixel_size * 15);

						sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString("TinyCode", text_font);
						canvas.drawTextBlob(
							blob, pixel_size * margin_size, pixel_size * (matrix.height() + margin_size * 5),
			   pixel_paint);
							*/

			// SkPixmap src;
			// bool success = bitmap.peekPixels(&src);
			// SkDynamicMemoryWStream dest;
			// SkPngEncoder::Options options;
			// success = SkPngEncoder::Encode(&dest, src, options);

			SkPixmap src;

			if(!bitmap.peekPixels(&src))
				return false;

			SkFILEWStream dest(path.c_str());
			SkPngEncoder::Options options;
			options.fZLibLevel = 9;

			if(!SkPngEncoder::Encode(&dest, src, options))
				return false;
			return true;
		}
	}
}