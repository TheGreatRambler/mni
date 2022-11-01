#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <chrono>
#include <fmt/core.h>
#include <fstream>
#include <mni.hpp>
#include <ostream>
#include <thread>

int main(int argc, char** argv) {
	CLI::App app { "A compiler and runtime for small Webassembly on QR codes" };
	app.require_subcommand(1, 1);

	auto& compile_sub = *app.add_subcommand("compile", "Compile into optimized Webassembly");
	std::string optimized_output_path;
	compile_sub.add_option(
		"-o,--output", optimized_output_path, "Compressed webassembly output (.owasm)");
	std::string qr_path;
	compile_sub.add_option("-q,--qr", qr_path, "QR code containing compressed webassembly (.png)");
	std::string wasm_input;
	compile_sub.add_option("wasm", wasm_input, "Webassembly module to compress")->required();

	auto& meta_sub  = *app.add_subcommand("meta", "Get metadata of optimized Webassembly");
	auto& meta_wasm = *meta_sub.add_option_group("wasm");
	std::string wasm_input_meta;
	meta_wasm.add_option("-w,--wasm", wasm_input_meta, "Optimized webassembly to run (.owasm)");
	std::string qr_path_meta;
	meta_wasm.add_option(
		"-q,--qr", qr_path_meta, "QR code containing compressed webassembly (.png)");
	meta_wasm.require_option(1);

	auto& run_sub  = *app.add_subcommand("run", "Run optimized Webassembly window");
	auto& run_wasm = *run_sub.add_option_group("wasm");
	std::string wasm_input_run;
	run_wasm.add_option("-w,--wasm", wasm_input_run, "Optimized webassembly to run (.owasm)");
	std::string qr_path_run;
	run_wasm.add_option("-q,--qr", qr_path_run, "QR code containing compressed webassembly (.png)");
	run_wasm.require_option(1);

	CLI11_PARSE(app, argc, argv);

	std::chrono::time_point<std::chrono::steady_clock> start;
	std::chrono::time_point<std::chrono::steady_clock> stop;
	uint64_t time_taken;

	if(compile_sub) {
		start = std::chrono::high_resolution_clock::now();
		std::ifstream wasm_file(wasm_input, std::ios::binary);
		std::vector<uint8_t> wasm_bytes(
			(std::istreambuf_iterator<char>(wasm_file)), std::istreambuf_iterator<char>());
		stop       = std::chrono::high_resolution_clock::now();
		time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
		fmt::print("Input wasm: {} bytes ({}ms)\n", wasm_bytes.size(), time_taken);

		std::vector<std::string> exported_functions;
		Mni::Wasm::GetExports(wasm_bytes, exported_functions);
		for(auto& name : exported_functions) {
			fmt::print("    {}\n", name);
		}

		std::vector<uint8_t> out;
		start = std::chrono::high_resolution_clock::now();
		Mni::Wasm::RemoveUnneccesary(wasm_bytes, out, Mni::Wasm::DEFINED_FUNCTIONS);
		stop       = std::chrono::high_resolution_clock::now();
		time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
		fmt::print("Purged wasm: {} bytes ({}ms)\n", out.size(), time_taken);

		Mni::Wasm::GetExports(out, exported_functions);
		for(auto& name : exported_functions) {
			fmt::print("    {}\n", name);
		}

		std::vector<uint8_t> out_optimized;
		start      = std::chrono::high_resolution_clock::now();
		auto size  = Mni::Wasm::NormalToOptimized(out, 0, out_optimized);
		stop       = std::chrono::high_resolution_clock::now();
		time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
		fmt::print(
			"Optimized wasm: {} bytes / {} bits ({}ms)\n", out_optimized.size(), size, time_taken);

		if(!optimized_output_path.empty()) {
			// Extension is generally .owasm (optimized wasm)
			start = std::chrono::high_resolution_clock::now();
			std::ofstream optimized_out(optimized_output_path, std::ios::out | std::ios::binary);
			optimized_out.write((const char*)out_optimized.data(), out_optimized.size());
			optimized_out.close();
			stop = std::chrono::high_resolution_clock::now();
			time_taken
				= std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
			fmt::print("Optimized Webassembly written ({}ms)\n", time_taken);
		}

		if(!qr_path.empty()) {
			start = std::chrono::high_resolution_clock::now();

			if(!Mni::Export::GenerateQRCode(size, out_optimized, 1000, 1000, qr_path)) {
				std::cerr << out_optimized.size()
						  << " bytes is too large for a QR code, 2953 bytes is the max"
						  << std::endl;
				exit(1);
			}

			stop = std::chrono::high_resolution_clock::now();
			time_taken
				= std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
			fmt::print("QR code written ({}ms)\n", time_taken);
		}
	} else if(meta_sub) {
		std::vector<uint8_t> optimized_wasm_bytes;
		if(!qr_path_meta.empty()) {
			start = std::chrono::high_resolution_clock::now();
			Mni::Import::ScanQRCode(optimized_wasm_bytes, qr_path_meta);
			stop = std::chrono::high_resolution_clock::now();
			time_taken
				= std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
			fmt::print(
				"Input optimized wasm: {} bytes ({}ms)\n", optimized_wasm_bytes.size(), time_taken);
		} else if(!wasm_input_meta.empty()) {
			start = std::chrono::high_resolution_clock::now();
			std::ifstream wasm_file(wasm_input_meta, std::ios::binary);
			optimized_wasm_bytes = std::vector<uint8_t>(
				(std::istreambuf_iterator<char>(wasm_file)), std::istreambuf_iterator<char>());
			stop = std::chrono::high_resolution_clock::now();
			time_taken
				= std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
			fmt::print(
				"Input optimized wasm: {} bytes ({}ms)\n", optimized_wasm_bytes.size(), time_taken);
		}

		start = std::chrono::high_resolution_clock::now();
		std::vector<uint8_t> wasm_bytes;
		auto size  = Mni::Wasm::OptimizedToNormal(wasm_bytes, 0, optimized_wasm_bytes);
		stop       = std::chrono::high_resolution_clock::now();
		time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
		fmt::print("Input wasm: {} bytes ({}ms)\n", wasm_bytes.size(), time_taken);

		start         = std::chrono::high_resolution_clock::now();
		auto metadata = Mni::Wasm::Runtime(wasm_bytes).Meta();
		stop          = std::chrono::high_resolution_clock::now();
		time_taken    = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
		fmt::print("Output: ({}ms)\n", time_taken);
		std::cout << "    name: " << metadata.name << std::endl;
	} else if(run_sub) {
		std::vector<uint8_t> optimized_wasm_bytes;
		if(!qr_path_run.empty()) {
			start = std::chrono::high_resolution_clock::now();
			Mni::Import::ScanQRCode(optimized_wasm_bytes, qr_path_run);
			stop = std::chrono::high_resolution_clock::now();
			time_taken
				= std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
			fmt::print(
				"Input optimized wasm: {} bytes ({}ms)\n", optimized_wasm_bytes.size(), time_taken);
		} else if(!wasm_input_run.empty()) {
			start = std::chrono::high_resolution_clock::now();
			std::ifstream wasm_file(wasm_input_run, std::ios::binary);
			optimized_wasm_bytes = std::vector<uint8_t>(
				(std::istreambuf_iterator<char>(wasm_file)), std::istreambuf_iterator<char>());
			stop = std::chrono::high_resolution_clock::now();
			time_taken
				= std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
			fmt::print(
				"Input optimized wasm: {} bytes ({}ms)\n", optimized_wasm_bytes.size(), time_taken);
		}

		start = std::chrono::high_resolution_clock::now();
		std::vector<uint8_t> wasm_bytes;
		auto size  = Mni::Wasm::OptimizedToNormal(wasm_bytes, 0, optimized_wasm_bytes);
		stop       = std::chrono::high_resolution_clock::now();
		time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
		fmt::print("Input wasm: {} bytes ({}ms)\n", wasm_bytes.size(), time_taken);

		std::vector<std::string> exported_functions;
		Mni::Wasm::GetExports(wasm_bytes, exported_functions);
		for(auto& name : exported_functions) {
			fmt::print("    {}\n", name);
		}

		Mni::Wasm::Runtime runtime(wasm_bytes);

		if(!runtime.PrepareWasm()) {
			return -1;
		}

		runtime.PrepareWindowStartup();

		while(runtime.TickWindow())
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		runtime.Close();
	}

	return 0;
}