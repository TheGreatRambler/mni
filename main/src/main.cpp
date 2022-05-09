#include <tinycode.hpp>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <numeric>
#include <vector>

#include "fmt/core.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include "stb_image.h"

#define QOI_IMPLEMENTATION
#include "qoi.h"

void testImages() {
	int width, height, channels;
	unsigned char* img = stbi_load("test.png", &width, &height, &channels, 4);

	std::cout << "Starting image size: " << width * height * 4 << std::endl;

	auto start = std::chrono::high_resolution_clock::now();

	std::vector<uint8_t> image(img, img + width * height * 4);
	std::vector<int64_t> image_input(image.begin(), image.end());

	std::vector<uint8_t> bytes;
	uint64_t current_bit = 0;
	current_bit          = TinyCode::Encoding::WriteNumUnsigned('q', 8, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteNumUnsigned('o', 8, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteNumUnsigned('i', 8, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteNumUnsigned('f', 8, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteNumUnsigned(width, 32, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteNumUnsigned(height, 32, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteNumUnsigned(4, 8, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteNumUnsigned(0, 8, current_bit, bytes);
	current_bit          = TinyCode::Encoding::WriteHuffmanIntegerList(image_input, current_bit, bytes);
	TinyCode::Encoding::FixLastByte(current_bit, bytes);

	auto stop = std::chrono::high_resolution_clock::now();
	fmt::print(
		"TinyCode took {} milliseconds\n", std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count());

	std::cout << "TinyCode bytes: " << (int)((int)current_bit / 8.0) << std::endl;

	qoi_desc qoi_description = {
		.width      = (unsigned int)width,
		.height     = (unsigned int)height,
		.channels   = 4,
		.colorspace = QOI_SRGB,
	};

	start   = std::chrono::high_resolution_clock::now();
	int res = qoi_write("test.qoi", img, &qoi_description);
	stop    = std::chrono::high_resolution_clock::now();
	fmt::print(
		"QOI took {} milliseconds\n", std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count());

	if(res == 0) {
		std::cout << "QOI writing failed" << std::endl;
	} else {
		std::cout << "QOI bytes: " << res << std::endl;
	}

	stbi_image_free(img);
}

int main(int, char*[]) {
	// auto start = std::chrono::high_resolution_clock::now();
	std::vector<int64_t> data2;
	// for(int i = 0; i < 100000; i++) {
	// std::vector<int64_t> data = { 0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144 };
	std::string test
		= "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Fusce pellentesque eleifend augue. Aenean cursus magna ac turpis fermentum, sed dapibus orci efficitur. Maecenas consequat ex tortor, at pellentesque libero eleifend eget. Proin nec elit sit amet ipsum commodo mattis. Vestibulum velit ex, faucibus id consectetur lacinia, faucibus a ipsum. Maecenas quis neque massa. Curabitur vitae molestie mi, eu tincidunt eros.Vivamus molestie fringilla nisl, eget blandit dolor commodo vel. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Proin et accumsan tortor, non dignissim lorem. Nullam vitae odio orci. Nunc tincidunt posuere velit, vitae porttitor justo interdum vel. Aliquam in lobortis diam. Fusce sit amet urna mollis, facilisis metus sit amet, pretium ex.Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nam eu libero mauris. Etiam suscipit, enim sed condimentum commodo, lorem enim maximus dolor, at ultrices odio libero sit amet purus. Duis sed velit pharetra, ultricies diam dapibus, auctor nibh. Aliquam erat volutpat. Vestibulum eget euismod augue. Duis id ante pellentesque, condimentum dui in, volutpat tortor. Vivamus mattis tincidunt eleifend. Vivamus pellentesque nec tortor et feugiat. Donec eleifend tortor sit amet viverra imperdiet. Nam iaculis neque id urna lacinia, eget volutpat ex congue. Nullam luctus, sapien quis dignissim interdum, erat risus commodo velit, id laoreet enim erat non lorem. In viverra gravida tortor, non lobortis arcu. Integer libero ligula, lobortis nec placerat in, sagittis sed purus. Nam lacinia lacus hendrerit, interdum lectus non, tristique leo.Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia curae; Integer porta facilisis tincidunt. Suspendisse potenti. Phasellus eget risus ut ex cursus suscipit. Nunc et maximus risus. Aliquam sed imperdiet augue. Mauris porttitor tincidunt malesuada. Pellentesque lectus nisl, placerat id est sit amet, scelerisque porttitor mi. Fusce dapibus facilisis libero, eu semper dui dignissim at.Morbi faucibus eros nisi, vel bibendum odio vestibulum sed. In ac sem in lorem ultricies condimentum. Integer eu posuere nibh. Nulla hendrerit lacinia dolor, et elementum nisl molestie convallis. Morbi nec leo non eros finibus pulvinar vitae a nunc. Sed non diam sed tellus tempus finibus ut in tortor. Duis nisl leo, mollis nec arcu non, aliquet condimentum turpis. Nullam aliquam orci at velit sagittis, ac varius dui auctor. Phasellus at lorem vel est aliquet finibus. Fusce at elementum ante.Mauris aliquam nisl nec mollis dignissim. Praesent condimentum risus eget nulla tincidunt, condimentum lacinia magna consequat. Aliquam erat volutpat. Pellentesque blandit ligula ut tortor venenatis, et dapibus diam aliquam. Ut scelerisque ultricies venenatis. Donec sed velit et lacus posuere pulvinar vitae non lorem. Sed tempus sem ut lorem varius mollis.Mauris ultricies varius est, sed mollis urna sodales nec. Etiam id urna vel libero fringilla facilisis vitae vel eu. ";
	std::vector<int64_t> data(test.begin(), test.end());

	std::vector<uint8_t> bytes;
	uint64_t current_bit = 0;
	current_bit          = TinyCode::Encoding::WriteHuffmanIntegerList(data, current_bit, bytes);
	TinyCode::Encoding::FixLastByte(current_bit, bytes);

	std::cout << "Huffman " << (int)current_bit << std::endl;
	std::cout << TinyCode::Debug::Print(current_bit, bytes, false) << std::endl;

	TinyCode::Export::GenerateQRCode(current_bit, bytes, 1000, 1000, "qrcode.png");

	std::vector<uint8_t> bytes2;
	TinyCode::Import::ScanQRCode(bytes2, "qrcode.png");
	std::cout << TinyCode::Debug::Print(current_bit, bytes2, false) << std::endl;

	if(TinyCode::Debug::AreIdentical(bytes, bytes2, current_bit)) {
		std::cout << "QR code data equal!" << std::endl;
	}

	data2.clear();
	TinyCode::Decoding::ReadHuffmanIntegerList(data2, 0, bytes);

	// for(int64_t num : data2) {
	//	std::cout << "    " << (int)num << std::endl;
	//}

	if(std::equal(data.begin(), data.end(), data2.begin())) {
		std::cout << "Vectors equal!" << std::endl;
	}
	//}
	// auto stop = std::chrono::high_resolution_clock::now();
	// fmt::print("Encoding and decoding took {} milliseconds\n",
	//	std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count());
	// testImages();

	std::ifstream testFile("test.wasm", std::ios::binary);
	std::vector<uint8_t> fileContents((std::istreambuf_iterator<char>(testFile)), std::istreambuf_iterator<char>());
	std::vector<uint8_t> out;

	TinyCode::Wasm::Optimize(fileContents, out);

	std::ofstream fout("test2.wasm", std::ios::out | std::ios::binary);
	fout.write((const char*)out.data(), out.size());
	fout.close();

	return 0;
}