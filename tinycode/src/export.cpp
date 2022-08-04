#include <tinycode.hpp>

#include <cstdint>
#include <string>

#include <BarcodeFormat.h>
#include <BitMatrix.h>
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

			// Can trigger crash on wasmtime Android (maybe)
			// {0x6A,0xD0,0x96,0x1C,0x00,0xC1,0x0E,0x18,0xC5,0x0C,0x12,0x9A,0x19,0x01,0x10,0x11,0x3E,0xE2,0x9A,0xDA,0xCA,0xDA,0xDE,0xE4,0xF2,0x40,0xBA,0xE8,0xCA,0xCA,0xDC,0xF2,0xC6,0xDE,0xC8,0xCA,0xBE,0xDC,0xC2,0xDA,0xCA,0x00,0xA8,0xF0,0xCB,0x05,0x04,0x00,0x01,0x85,0xAC,0xE0,0xC3,0x10,0x40,0x00,0x18,0x58,0x01,0xA4,0x32,0xB6,0x36,0x37,0x90,0x3B,0xB7,0xB9,0x36,0x32,0x10,0x90,0x2A,0x34,0x34,0xB9,0x90,0x34,0xB9,0x90,0x2A,0x32,0xB2,0xB7,0x3C,0xA1,0xB7,0xB2,0x32,0xB9,0x90,0x80}

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