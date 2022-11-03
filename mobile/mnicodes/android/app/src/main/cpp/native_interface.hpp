#pragma once

#include <android/log.h>
#include <mni.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class NativeInterface {
public:
	NativeInterface() { }

	bool LoadFromBuffer(uint8_t* buffer, size_t size);
	bool RenderNextFrame();
	std::string& GetCodeName();

	void SetRotation(int angle);
	void SetPress(float x, float y);

private:
	std::vector<uint8_t> optimized_wasm_bytes;
	std::vector<uint8_t> wasm_bytes;
	Mni::Wasm::Metadata meta;
	std::thread render_thread;
	std::shared_ptr<Mni::Wasm::Runtime> runtime;
};

static NativeInterface* interface;