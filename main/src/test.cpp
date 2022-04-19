#include <tinycode.hpp>

#include <chrono>
#include <cstdlib>
#include <ctime>

#include "fmt/core.h"

void test() {
	srand(time(0));

	auto start = std::chrono::high_resolution_clock::now();

	for(int i = 0; i < 100000; i++) {
		std::vector<int64_t> data;
		for(int i = 0; i < 20; i++) {
			data.push_back(rand() % 1000 - 500);
		}

		// for(int64_t num : data) {
		//	std::cout << "    " << (int)num << std::endl;
		//}

		std::vector<uint8_t> bytes;
		uint64_t current_bit = 0;
		current_bit          = TinyCode::Encoding::WriteIdealIntegerList(data, current_bit, bytes);
		TinyCode::Encoding::FixLastByte(current_bit, bytes);
	}
	auto stop = std::chrono::high_resolution_clock::now();
	fmt::print(
		"Encoding took {} milliseconds\n", std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count());

	std::vector<int64_t> data;
	for(int i = 0; i < 20; i++) {
		data.push_back(rand() % 1000 - 500);
	}

	// for(int64_t num : data) {
	//	std::cout << "    " << (int)num << std::endl;
	//}

	std::vector<uint8_t> bytes;
	uint64_t current_bit = 0;
	current_bit          = TinyCode::Encoding::WriteIdealIntegerList(data, current_bit, bytes);
	TinyCode::Encoding::FixLastByte(current_bit, bytes);

	start = std::chrono::high_resolution_clock::now();
	std::vector<int64_t> data2;
	for(int i = 0; i < 100000; i++) {
		// std::cout << (int)current_bit << std::endl;
		// std::cout << TinyCode::Debug::Print(current_bit, bytes, false) << std::endl;

		data2.clear();
		TinyCode::Decoding::ReadIdealIntegerList(data2, 0, bytes);

		// for(int64_t num : data2) {
		//	std::cout << "    " << (int)num << std::endl;
		//}

		// if(std::equal(data.begin(), data.end(), data2.begin())) {
		//	std::cout << "Vectors equal!" << std::endl;
		//}
	}
	stop = std::chrono::high_resolution_clock::now();
	fmt::print(
		"Decoding took {} milliseconds\n", std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count());
}