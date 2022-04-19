#include <tinycode.hpp>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

#include "fmt/core.h"

int main(int, char*[]) {
	// auto start = std::chrono::high_resolution_clock::now();
	std::vector<int64_t> data2;
	// for(int i = 0; i < 100000; i++) {
	// std::vector<int64_t> data = { 0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144 };
	std::string test
		= "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer a molestie mauris. Duis hendrerit quam eget tortor iaculis suscipit. Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. Praesent imperdiet porttitor diam. Suspendisse viverra, metus id ornare varius, enim ligula egestas diam, eget lobortis nibh mi vestibulum lorem. Praesent varius, enim vitae porta posuere, orci felis pretium ligula, quis facilisis urna magna in diam. Proin interdum libero eu libero dictum dignissim. Aenean auctor eu dui id cursus. Nulla vitae odio pretium, vehicula purus sed, commodo odio. Vivamus iaculis dolor libero, nec ultrices augue dapibus non. Etiam venenatis ultrices sem, in fermentum nulla consectetur finibus. Sed tortor lectus, laoreet vel euismod non, mollis vitae elit. Ut pharetra, enim vel porta hendrerit, magna orci aliquam arcu, convallis euismod lorem metus in velit";
	std::vector<int64_t> data(test.begin(), test.end());

	std::vector<uint8_t> bytes;
	uint64_t current_bit = 0;
	current_bit          = TinyCode::Encoding::WriteIdealIntegerList(data, current_bit, bytes);
	TinyCode::Encoding::FixLastByte(current_bit, bytes);

	std::cout << (int)current_bit << std::endl;
	std::cout << TinyCode::Debug::Print(current_bit, bytes, false) << std::endl;

	data2.clear();
	TinyCode::Decoding::ReadIdealIntegerList(data2, 0, bytes);

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

	return 0;
}