#include <gtest/gtest.h>
#include <tinycode.hpp>

#include <cstdint>
#include <iostream>
#include <random>

// Test MoveBits
TEST(Encoding, MoveBits) {
	std::mt19937 rng(1);
	auto bit_dist  = std::uniform_int_distribution { 0, 1 };
	auto byte_dist = std::uniform_int_distribution { 0, 7 };
	auto size_dist = std::uniform_int_distribution { 50, 100 };

	std::vector<uint8_t> bytes;
	uint64_t current_bit = 0;

	constexpr int NUM_SHIFTS = 10000;

	// Verify that all kinds of right movements work as expected
	for(int i = 0; i < NUM_SHIFTS; i++) {
		// Create randomly sized data to work with
		current_bit = 0;
		for(int i = 0; i < size_dist(rng); i++) {
			current_bit = TinyCode::Encoding::Write1Bit(bit_dist(rng), current_bit, bytes);
		}

		uint8_t src_shift     = byte_dist(rng);
		uint8_t dest_shift    = byte_dist(rng) + 8;
		std::string pre_moved = TinyCode::Debug::Print(current_bit, bytes, false).substr(src_shift);
		std::string pre_unmoved = TinyCode::Debug::Print(dest_shift, bytes, false);
		current_bit = TinyCode::Encoding::MoveBits(src_shift, current_bit, dest_shift, bytes);
		std::string post_moved
			= TinyCode::Debug::Print(current_bit, bytes, false).substr(dest_shift);
		std::string post_unmoved = TinyCode::Debug::Print(dest_shift, bytes, false);

		EXPECT_STREQ(pre_moved.c_str(), post_moved.c_str());
		EXPECT_STREQ(pre_unmoved.c_str(), post_unmoved.c_str());
	}

	// Verify that all kinds of left movements work as expected
	for(int i = 0; i < NUM_SHIFTS; i++) {
		// Create randomly sized data to work with
		current_bit = 0;
		for(int i = 0; i < size_dist(rng); i++) {
			current_bit = TinyCode::Encoding::Write1Bit(bit_dist(rng), current_bit, bytes);
		}

		uint64_t old                 = current_bit;
		uint8_t src_shift            = byte_dist(rng) + 8;
		uint8_t dest_shift           = byte_dist(rng);
		uint64_t right_unmoved_start = current_bit - src_shift + dest_shift;
		std::string pre_moved = TinyCode::Debug::Print(current_bit, bytes, false).substr(src_shift);
		std::string pre_unmoved_right
			= TinyCode::Debug::Print(current_bit, bytes, false).substr(right_unmoved_start);
		std::string pre_unmoved_left = TinyCode::Debug::Print(dest_shift, bytes, false);
		current_bit = TinyCode::Encoding::MoveBits(src_shift, current_bit, dest_shift, bytes);
		std::string post_moved
			= TinyCode::Debug::Print(current_bit, bytes, false).substr(dest_shift);
		std::string post_unmoved_right
			= TinyCode::Debug::Print(old, bytes, false).substr(right_unmoved_start);
		std::string post_unmoved_left = TinyCode::Debug::Print(dest_shift, bytes, false);

		EXPECT_STREQ(pre_moved.c_str(), post_moved.c_str());
		EXPECT_STREQ(pre_unmoved_right.c_str(), post_unmoved_right.c_str());
		EXPECT_STREQ(pre_unmoved_left.c_str(), post_unmoved_left.c_str());
	}
}
