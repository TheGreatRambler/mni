#pragma once

#include <tinycode.hpp>
#include <tinycode/tree.hpp>

#include <bit>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "wasm-binary.h"
#include "wasm.h"

namespace TinyCode {
	namespace Wasm {
#define DEBUG_TYPE "binary"

		using namespace wasm;

		// Default variables
		const uint8_t leb_width = 5;

		enum class ValueWritten {
			// Webassembly header
			Magic,
			Version,
			// Section header
			SectionSize,
			SectionStart,
			// Function
			FunctionIndex,
			CountFunctionIndices,
			NumFunctionParams,
			CountDefinedFunctions,
			// Function local
			LocalIndex,
			NumFunctionLocals,
			FunctionLocalSize,
			CountNumLocalsByType,
			NumLocalsByType,
			// Type
			CountTypeGroups,
			Type,
			TypeIndex,
			CountTypeNames,
			NumTypes,
			RTTDepth,
			// Table
			TableIndex,
			CountTables,
			CountDefinedTables,
			// Element segment
			ElementSegmentIndex,
			CountElementSegments,
			ElementSegmentFlags,
			ElementSegmentType,
			ElementSegmentSize,
			// Memory
			MemoryIndex,
			CountMemories,
			MemorySegmentIndex,
			CountMemorySegments,
			MemorySegmentFlags,
			// Import
			CountImports,
			// Export
			CountExports,
			// Global
			GlobalIndex,
			CountGlobals,
			// GC field
			GCFieldIndex,
			NumGCFields,
			CountGCFieldTypes,
			// External
			ExternalKind,
			// Tag
			TagIndex,
			CountDefinedTags,
			// Attribute
			Attribute,
			Mutable,
			// Heap type
			HeapType,
			// AST Node
			ASTNode,
			ASTNode32,
			// Struct
			StructFieldIndex,
			NumStructFields,
			// Array
			ArraySize,
			// User section
			UserSectionData,
			// Features
			FeaturePrefix,
			NumFeatures,
			// Dynlink
			DylinkSection,
			NumNeededDynlibs,
			// Literals
			ConstS32,
			ConstS64,
			ConstF32,
			ConstF64,
			ConstV128,
			// SIMD
			SIMDIndex,
			AtomicFenceOrder,
			// Memory access
			MemoryAccessAlignment,
			MemoryAccessOffset,
			// Break
			BreakIndex,
			// Switch
			SwitchTargets,
			// Select
			NumSelectTypes,
			// memory.size
			MemorySizeFlags,
			// memory.grow
			MemoryGrowFlags,
			// Scratch local
			ScratchLocalIndex,
			// Buffer
			InlineBufferSize,
		};

		static const char* ValueWrittenName[] = {
			// Webassembly header
			"Magic",
			"Version",
			// Section header
			"SectionSize",
			"SectionStart",
			// Function
			"FunctionIndex",
			"CountFunctionIndices",
			"NumFunctionParams",
			"CountDefinedFunctions",
			// Function local
			"LocalIndex",
			"NumFunctionLocals",
			"FunctionLocalSize",
			"CountNumLocalsByType",
			"NumLocalsByType",
			// Type
			"CountTypeGroups",
			"Type",
			"TypeIndex",
			"CountTypeNames",
			"NumTypes",
			"RTTDepth",
			// Table
			"TableIndex",
			"CountTables",
			"CountDefinedTables",
			// Element segment
			"ElementSegmentIndex",
			"CountElementSegments",
			"ElementSegmentFlags",
			"ElementSegmentType",
			"ElementSegmentSize",
			// Memory
			"MemoryIndex",
			"CountMemories",
			"MemorySegmentIndex",
			"CountMemorySegments",
			"MemorySegmentFlags",
			// Import
			"CountImports",
			// Export
			"CountExports",
			// Global
			"GlobalIndex",
			"CountGlobals",
			// GC field
			"GCFieldIndex",
			"NumGCFields",
			"CountGCFieldTypes",
			// External
			"ExternalKind",
			// Tag
			"TagIndex",
			"CountDefinedTags",
			// Attribute
			"Attribute",
			"Mutable",
			// Heap type
			"HeapType",
			// AST Node
			"ASTNode",
			"ASTNode32",
			// Struct
			"StructFieldIndex",
			"NumStructFields",
			// Array
			"ArraySize",
			// User section
			"UserSectionData",
			// Features
			"FeaturePrefix",
			"NumFeatures",
			// Dynlink
			"DylinkSection",
			"NumNeededDynlibs",
			// Literals
			"ConstS32",
			"ConstS64",
			"ConstF32",
			"ConstF64",
			"ConstV128",
			// SIMD
			"SIMDIndex",
			"AtomicFenceOrder",
			// Memory access
			"MemoryAccessAlignment",
			"MemoryAccessOffset",
			// Break
			"BreakIndex",
			// Switch
			"SwitchTargets",
			// Select
			"NumSelectTypes",
			// memory.size
			"MemorySizeFlags",
			// memory.grow
			"MemoryGrowFlags",
			// Scratch local
			"ScratchLocalIndex",
			// Buffer
			"InlineBufferSize",
		};

		enum class CompressionType { LEB = 0b00, HUFFMAN = 0b01, FIXED_WIDTH = 0b10 };

		struct CompressionTechnique {
			CompressionType type;
		};

		static std::unordered_map<ValueWritten, CompressionTechnique> default_technique = {
			{ ValueWritten::FunctionIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::Type, { CompressionType::HUFFMAN } },
			{ ValueWritten::NumTypes, { CompressionType::HUFFMAN } },
			{ ValueWritten::NumFunctionParams, { CompressionType::HUFFMAN } },
			{ ValueWritten::NumStructFields, { CompressionType::HUFFMAN } },
			{ ValueWritten::TypeIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::ASTNode, { CompressionType::HUFFMAN } },
			{ ValueWritten::ElementSegmentSize, { CompressionType::HUFFMAN } },
			{ ValueWritten::NumFunctionLocals, { CompressionType::HUFFMAN } },
			{ ValueWritten::TableIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::GlobalIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::TagIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::ElementSegmentIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::NumGCFields, { CompressionType::HUFFMAN } },
			{ ValueWritten::GCFieldIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::RTTDepth, { CompressionType::HUFFMAN } },
			{ ValueWritten::HeapType, { CompressionType::HUFFMAN } },
			{ ValueWritten::BreakIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::LocalIndex, { CompressionType::LEB } },
			{ ValueWritten::ASTNode32, { CompressionType::HUFFMAN } },
			{ ValueWritten::SwitchTargets, { CompressionType::HUFFMAN } },
			{ ValueWritten::AtomicFenceOrder, { CompressionType::HUFFMAN } },
			{ ValueWritten::ConstS32, { CompressionType::LEB } },
			{ ValueWritten::ConstS64, { CompressionType::LEB } },
			{ ValueWritten::ConstF32, { CompressionType::LEB } },
			{ ValueWritten::ConstF64, { CompressionType::LEB } },
			{ ValueWritten::NumSelectTypes, { CompressionType::HUFFMAN } },
			{ ValueWritten::ScratchLocalIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::MemorySegmentIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::InlineBufferSize, { CompressionType::LEB } },
			{ ValueWritten::StructFieldIndex, { CompressionType::HUFFMAN } },
			{ ValueWritten::ArraySize, { CompressionType::HUFFMAN } },
			{ ValueWritten::FunctionLocalSize, { CompressionType::HUFFMAN } },
			{ ValueWritten::MemoryAccessAlignment, { CompressionType::HUFFMAN } },
			{ ValueWritten::MemoryAccessOffset, { CompressionType::HUFFMAN } },
		};

		class OptimizedWasmBinaryWriter {
		public:
			OptimizedWasmBinaryWriter(std::vector<uint8_t>& bytes, uint64_t current_bit, Module* wasm)
				: bytes(bytes)
				, original_current_bit(current_bit)
				, current_bit(current_bit)
				, wasm(wasm) { }

			template <ValueWritten VT, typename T = int> void writeValue(T value = 0) {
				if(creatingHuffman) {
					if constexpr(VT == ValueWritten::FunctionIndex || VT == ValueWritten::Type
								 || VT == ValueWritten::NumTypes || VT == ValueWritten::NumFunctionParams
								 || VT == ValueWritten::NumStructFields || VT == ValueWritten::TypeIndex
								 || VT == ValueWritten::ASTNode || VT == ValueWritten::ElementSegmentSize
								 || VT == ValueWritten::NumFunctionLocals || VT == ValueWritten::TableIndex
								 || VT == ValueWritten::GlobalIndex || VT == ValueWritten::TagIndex
								 || VT == ValueWritten::ElementSegmentIndex || VT == ValueWritten::NumGCFields
								 || VT == ValueWritten::GCFieldIndex || VT == ValueWritten::RTTDepth
								 || VT == ValueWritten::HeapType || VT == ValueWritten::BreakIndex
								 || VT == ValueWritten::LocalIndex || VT == ValueWritten::ASTNode32
								 || VT == ValueWritten::SwitchTargets || VT == ValueWritten::AtomicFenceOrder
								 || VT == ValueWritten::ConstS32 || VT == ValueWritten::ConstS64
								 || VT == ValueWritten::ConstF32 || VT == ValueWritten::ConstF64
								 || VT == ValueWritten::NumSelectTypes || VT == ValueWritten::ScratchLocalIndex
								 || VT == ValueWritten::MemorySegmentIndex || VT == ValueWritten::InlineBufferSize
								 || VT == ValueWritten::StructFieldIndex || VT == ValueWritten::ArraySize
								 || VT == ValueWritten::FunctionLocalSize || VT == ValueWritten::MemoryAccessAlignment
								 || VT == ValueWritten::MemoryAccessOffset) {
						// Huffman and other optimizations are attempted
						seen[VT].push_back(value);
					}
				} else {
					if constexpr(VT == ValueWritten::CountMemories || VT == ValueWritten::CountTypeGroups
								 || VT == ValueWritten::CountImports || VT == ValueWritten::CountDefinedFunctions
								 || VT == ValueWritten::CountGlobals || VT == ValueWritten::CountExports
								 || VT == ValueWritten::CountMemorySegments || VT == ValueWritten::CountDefinedTables
								 || VT == ValueWritten::CountElementSegments || VT == ValueWritten::CountDefinedTags
								 || VT == ValueWritten::CountFunctionIndices || VT == ValueWritten::CountTypeNames
								 || VT == ValueWritten::CountTables || VT == ValueWritten::CountGCFieldTypes
								 || VT == ValueWritten::NumFeatures || VT == ValueWritten::NumNeededDynlibs
								 || VT == ValueWritten::CountNumLocalsByType || VT == ValueWritten::NumLocalsByType) {
						// Small number used very few times, stick to LEB
						current_bit = TinyCode::Encoding::WriteLEBUnsigned(value, leb_width, current_bit, bytes);
					} else if constexpr(VT == ValueWritten::FunctionIndex || VT == ValueWritten::Type
										|| VT == ValueWritten::NumTypes || VT == ValueWritten::NumFunctionParams
										|| VT == ValueWritten::NumStructFields || VT == ValueWritten::TypeIndex
										|| VT == ValueWritten::ASTNode || VT == ValueWritten::ElementSegmentSize
										|| VT == ValueWritten::NumFunctionLocals || VT == ValueWritten::TableIndex
										|| VT == ValueWritten::GlobalIndex || VT == ValueWritten::TagIndex
										|| VT == ValueWritten::ElementSegmentIndex || VT == ValueWritten::NumGCFields
										|| VT == ValueWritten::GCFieldIndex || VT == ValueWritten::RTTDepth
										|| VT == ValueWritten::HeapType || VT == ValueWritten::BreakIndex
										|| VT == ValueWritten::LocalIndex || VT == ValueWritten::ASTNode32
										|| VT == ValueWritten::SwitchTargets || VT == ValueWritten::AtomicFenceOrder
										|| VT == ValueWritten::ConstS32 || VT == ValueWritten::ConstS64
										|| VT == ValueWritten::ConstF32 || VT == ValueWritten::ConstF64
										|| VT == ValueWritten::NumSelectTypes || VT == ValueWritten::ScratchLocalIndex
										|| VT == ValueWritten::MemorySegmentIndex
										|| VT == ValueWritten::InlineBufferSize || VT == ValueWritten::StructFieldIndex
										|| VT == ValueWritten::ArraySize || VT == ValueWritten::FunctionLocalSize
										|| VT == ValueWritten::MemoryAccessAlignment
										|| VT == ValueWritten::MemoryAccessOffset) {
						// Huffman and other optimizations are attempted
						if(default_technique[VT].type == CompressionType::HUFFMAN) {
							current_bit = TinyCode::Encoding::WriteNumUnsigned(huffman_mapping[value].representation,
								huffman_mapping[value].bit_size, current_bit, bytes);
						} else if(default_technique[VT].type == CompressionType::LEB) {
							if constexpr(VT == ValueWritten::Type || VT == ValueWritten::HeapType
										 || VT == ValueWritten::ConstS32 || VT == ValueWritten::ConstS64
										 || VT == ValueWritten::ConstF32 || VT == ValueWritten::ConstF64) {
								// Signed values
								current_bit = TinyCode::Encoding::WriteLEB(value, leb_width, current_bit, bytes);
							} else {
								current_bit
									= TinyCode::Encoding::WriteLEBUnsigned(value, leb_width, current_bit, bytes);
							}
						}
					} else if constexpr(VT == ValueWritten::Magic || VT == ValueWritten::Version
										|| VT == ValueWritten::Attribute || VT == ValueWritten::ElementSegmentType
										|| VT == ValueWritten::FeaturePrefix || VT == ValueWritten::MemoryIndex
										|| VT == ValueWritten::MemorySizeFlags || VT == ValueWritten::MemoryGrowFlags) {
						// Value has no significance in current Webassembly version, ignore entirely
					} else {
						if constexpr(VT == ValueWritten::Mutable) {
							// Boolean
							current_bit = TinyCode::Encoding::Write1Bit(value, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::SectionSize) {
							// Default U32LEB
							current_bit = TinyCode::Encoding::WriteNumUnsigned(0, 5 * 8, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::SectionStart) {
							// 4 bit enum
							current_bit = TinyCode::Encoding::WriteNumUnsigned(value, 4, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::ExternalKind) {
							// 2 bit enum
							current_bit = TinyCode::Encoding::WriteNumUnsigned(int32_t(value), 2, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::MemorySegmentFlags) {
							// 3 bit enum
							current_bit = TinyCode::Encoding::WriteNumUnsigned(value, 3, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::ElementSegmentFlags) {
							// 3 bit enum
							current_bit = TinyCode::Encoding::WriteNumUnsigned(value, 3, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::UserSectionData) {
							// Raw bytes (not sure if char is signed, TODO)
							current_bit = TinyCode::Encoding::WriteNumUnsigned(value, 8, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::DylinkSection) {
							current_bit = TinyCode::Encoding::WriteLEBUnsigned(
								wasm->dylinkSection->memorySize, leb_width, current_bit, bytes);
							current_bit = TinyCode::Encoding::WriteLEBUnsigned(
								wasm->dylinkSection->memoryAlignment, leb_width, current_bit, bytes);
							current_bit = TinyCode::Encoding::WriteLEBUnsigned(
								wasm->dylinkSection->tableSize, leb_width, current_bit, bytes);
							current_bit = TinyCode::Encoding::WriteLEBUnsigned(
								wasm->dylinkSection->tableAlignment, leb_width, current_bit, bytes);
						} else if constexpr(VT == ValueWritten::ConstV128) {
							std::array<uint8_t, 16> v = value.getv128();
							for(size_t i = 0; i < 16; ++i) {
								current_bit = TinyCode::Encoding::WriteNumUnsigned(v[i], 8, current_bit, bytes);
							}
						} else if constexpr(VT == ValueWritten::SIMDIndex) {
							// Guaranteed to be 5 bits or less
							current_bit = TinyCode::Encoding::WriteNumUnsigned(value, 5, current_bit, bytes);
						} else {
							std::cout << ValueWrittenName[(int)VT] << " was accidentally ignored" << std::endl;
						}
					}
				}
			}

			void determineWritingSchemes() {
				/*
				for (auto const& [type, nums] : seen) {
					std::unordered_map<int64_t, TinyCode::Tree::Node> huffman_frequencies;
					uint64_t leb_total_size = 0;
					for (auto& num : nums) {

					}
				}
				*/

				// All huffman for now
				std::unordered_map<int64_t, TinyCode::Tree::Node> huffman_frequencies;
				for(auto const& [type, values] : seen) {
					if(default_technique[type].type == CompressionType::HUFFMAN) {
						for(int64_t num : values) {
							if(huffman_frequencies.count(num)) {
								huffman_frequencies[num].freq++;
							} else {
								huffman_frequencies[num] = TinyCode::Tree::Node(num, 1);
							}
						}
					}
				}

				TinyCode::Tree::GenerateHuffmanFrequencies(huffman_frequencies, huffman_mapping);
				current_bit = TinyCode::Encoding::WriteHuffmanHeader(huffman_mapping, current_bit, bytes);
			}

			uint64_t finish() {
				int64_t wasm_size = current_bit - original_current_bit;
				uint64_t offset   = TinyCode::Encoding::GetRequiredLEBBits(wasm_size, leb_width);
				current_bit       = TinyCode::Encoding::MoveBits(
						  original_current_bit, current_bit, original_current_bit + offset, bytes);
				TinyCode::Encoding::WriteLEBUnsigned(wasm_size, leb_width, original_current_bit, bytes);
				return current_bit;
			}

			void writeInlineString(const char* name) {
				int32_t size = strlen(name);
				current_bit  = TinyCode::Encoding::WriteLEBUnsigned(size, leb_width, current_bit, bytes);
				writeData(name, size);
			}

			void writeData(const char* data, size_t size) {
				for(size_t i = 0; i < size; i++) {
					current_bit = TinyCode::Encoding::WriteNum(data[i], 8, current_bit, bytes);
				}
			}

			void writeResizableLimits(Address initial, Address maximum, bool hasMaximum, bool shared, bool is64) {
				uint32_t flags = (hasMaximum ? (uint32_t)BinaryConsts::HasMaximum : 0U)
								 | (shared ? (uint32_t)BinaryConsts::IsShared : 0U)
								 | (is64 ? (uint32_t)BinaryConsts::Is64 : 0U);

				current_bit = TinyCode::Encoding::WriteNumUnsigned(flags, 3, current_bit, bytes);
				current_bit = TinyCode::Encoding::WriteLEBUnsigned(initial, leb_width, current_bit, bytes);
				if(hasMaximum) {
					current_bit = TinyCode::Encoding::WriteLEBUnsigned(hasMaximum, leb_width, current_bit, bytes);
				}
			}

			uint8_t maxSectionSizeBits() {
				return 5 * 8;
			}

			int32_t streamOffset() {
				return current_bit;
			}

			size_t streamWrite(size_t loc, int32_t value) {
				return TinyCode::Encoding::WriteLEBUnsigned(value, leb_width, loc, bytes) - loc;
			}

			void streamMove(size_t start, size_t end, size_t dest) {
				current_bit = TinyCode::Encoding::MoveBits(start, end, dest, bytes);
			}

			void setHuffmanCreation(bool huffman) {
				creatingHuffman = huffman;
			}

			bool isHuffmanCreation() {
				return creatingHuffman;
			}

			void clear() {
				current_bit = original_current_bit;
			}

		private:
			bool creatingHuffman = false;
			std::vector<uint8_t>& bytes;
			uint64_t original_current_bit;
			uint64_t current_bit;
			Module* wasm;

			std::unordered_map<ValueWritten, std::vector<int64_t>> seen;
			std::unordered_map<int64_t, TinyCode::Tree::NodeRepresentation> huffman_mapping;
		};

		class OptimizedWasmBinaryReader {
		public:
			OptimizedWasmBinaryReader(std::vector<uint8_t>& bytes, uint64_t current_bit, Module& wasm)
				: bytes(bytes)
				, current_bit(current_bit)
				, wasm(wasm) { }

			template <ValueWritten VT, typename I = int> auto getValue(I inputValue = 0) {
				WASM_UNUSED(inputValue);
				if constexpr(VT == ValueWritten::CountMemories || VT == ValueWritten::CountTypeGroups
							 || VT == ValueWritten::CountImports || VT == ValueWritten::CountDefinedFunctions
							 || VT == ValueWritten::CountGlobals || VT == ValueWritten::CountExports
							 || VT == ValueWritten::CountMemorySegments || VT == ValueWritten::CountDefinedTables
							 || VT == ValueWritten::CountElementSegments || VT == ValueWritten::CountDefinedTags
							 || VT == ValueWritten::CountFunctionIndices || VT == ValueWritten::CountTypeNames
							 || VT == ValueWritten::CountTables || VT == ValueWritten::CountGCFieldTypes
							 || VT == ValueWritten::NumFeatures || VT == ValueWritten::NumNeededDynlibs
							 || VT == ValueWritten::CountNumLocalsByType || VT == ValueWritten::NumLocalsByType) {
					// Small number used very few times, stick to LEB
					int64_t value;
					current_bit = TinyCode::Decoding::ReadLEBUnsigned(&value, leb_width, current_bit, bytes);
					return value;
				} else if constexpr(VT == ValueWritten::FunctionIndex || VT == ValueWritten::Type
									|| VT == ValueWritten::NumTypes || VT == ValueWritten::NumFunctionParams
									|| VT == ValueWritten::NumStructFields || VT == ValueWritten::TypeIndex
									|| VT == ValueWritten::ASTNode || VT == ValueWritten::ElementSegmentSize
									|| VT == ValueWritten::NumFunctionLocals || VT == ValueWritten::TableIndex
									|| VT == ValueWritten::GlobalIndex || VT == ValueWritten::TagIndex
									|| VT == ValueWritten::ElementSegmentIndex || VT == ValueWritten::NumGCFields
									|| VT == ValueWritten::GCFieldIndex || VT == ValueWritten::RTTDepth
									|| VT == ValueWritten::HeapType || VT == ValueWritten::BreakIndex
									|| VT == ValueWritten::LocalIndex || VT == ValueWritten::ASTNode32
									|| VT == ValueWritten::SwitchTargets || VT == ValueWritten::AtomicFenceOrder
									|| VT == ValueWritten::ConstS32 || VT == ValueWritten::ConstS64
									|| VT == ValueWritten::ConstF32 || VT == ValueWritten::ConstF64
									|| VT == ValueWritten::NumSelectTypes || VT == ValueWritten::ScratchLocalIndex
									|| VT == ValueWritten::MemorySegmentIndex || VT == ValueWritten::InlineBufferSize
									|| VT == ValueWritten::StructFieldIndex || VT == ValueWritten::ArraySize
									|| VT == ValueWritten::FunctionLocalSize
									|| VT == ValueWritten::MemoryAccessAlignment
									|| VT == ValueWritten::MemoryAccessOffset) {
					// Huffman and other optimizations are attempted
					int64_t return_num;
					if(default_technique[VT].type == CompressionType::HUFFMAN) {
						current_bit
							= TinyCode::Decoding::ReadHuffmanValue(huffman_root, &return_num, current_bit, bytes);
						std::cout << ValueWrittenName[(int)VT] << " is " << return_num << std::endl;

						if constexpr(VT == ValueWritten::ConstS32 || VT == ValueWritten::ConstS64
									 || VT == ValueWritten::ConstF32 || VT == ValueWritten::ConstF64) {
							return wasm::Literal((int32_t)return_num);
						} else {
							return return_num;
						}
					} else if(default_technique[VT].type == CompressionType::LEB) {
						if constexpr(VT == ValueWritten::Type || VT == ValueWritten::HeapType) {
							// Signed values
							current_bit = TinyCode::Decoding::ReadLEB(&return_num, leb_width, current_bit, bytes);
							return return_num;
						} else if constexpr(VT == ValueWritten::ConstS32 || VT == ValueWritten::ConstS64
											|| VT == ValueWritten::ConstF32 || VT == ValueWritten::ConstF64) {
							current_bit = TinyCode::Decoding::ReadLEB(&return_num, leb_width, current_bit, bytes);
							return wasm::Literal((int32_t)return_num);
						} else {
							current_bit
								= TinyCode::Decoding::ReadLEBUnsigned(&return_num, leb_width, current_bit, bytes);
							return return_num;
						}
					}
				} else {
					int64_t return_num;
					if constexpr(VT == ValueWritten::Magic) {
						return wasm::BinaryConsts::Magic;
					} else if constexpr(VT == ValueWritten::Version) {
						return wasm::BinaryConsts::Version;
					} else if constexpr(VT == ValueWritten::Attribute) {
						return 0;
					} else if constexpr(VT == ValueWritten::ElementSegmentType) {
						return 0;
					} else if constexpr(VT == ValueWritten::FeaturePrefix) {
						return wasm::BinaryConsts::FeatureUsed;
					} else if constexpr(VT == ValueWritten::MemoryIndex) {
						return 0;
					} else if constexpr(VT == ValueWritten::MemorySizeFlags) {
						return 0;
					} else if constexpr(VT == ValueWritten::MemoryGrowFlags) {
						return 0;
					} else if constexpr(VT == ValueWritten::Mutable) {
						bool return_bool;
						current_bit = TinyCode::Decoding::Read1Bit(&return_bool, current_bit, bytes);
						return return_bool;
					} else if constexpr(VT == ValueWritten::SectionSize) {
						// Default U32LEB
						current_bit = TinyCode::Decoding::ReadLEBUnsigned(&return_num, leb_width, current_bit, bytes);
						return return_num;
					} else if constexpr(VT == ValueWritten::SectionStart) {
						// 4 bit enum
						current_bit = TinyCode::Decoding::ReadNumUnsigned(&return_num, 4, current_bit, bytes);
						return return_num;
					} else if constexpr(VT == ValueWritten::ExternalKind) {
						// 2 bit enum
						current_bit = TinyCode::Decoding::ReadNumUnsigned(&return_num, 2, current_bit, bytes);
						return (wasm::ExternalKind)return_num;
					} else if constexpr(VT == ValueWritten::MemorySegmentFlags) {
						// 3 bit enum
						current_bit = TinyCode::Decoding::ReadNumUnsigned(&return_num, 3, current_bit, bytes);
						return return_num;
					} else if constexpr(VT == ValueWritten::ElementSegmentFlags) {
						// 3 bit enum
						current_bit = TinyCode::Decoding::ReadNumUnsigned(&return_num, 3, current_bit, bytes);
						return return_num;
					} else if constexpr(VT == ValueWritten::UserSectionData) {
						// Raw bytes (not sure if char is signed, TODO)
						current_bit = TinyCode::Decoding::ReadNumUnsigned(&return_num, 8, current_bit, bytes);
						return return_num;
					} else if constexpr(VT == ValueWritten::DylinkSection) {
						current_bit = TinyCode::Decoding::ReadLEBUnsigned(&return_num, leb_width, current_bit, bytes);
						wasm.dylinkSection->memorySize = return_num;
						current_bit = TinyCode::Decoding::ReadLEBUnsigned(&return_num, leb_width, current_bit, bytes);
						wasm.dylinkSection->memoryAlignment = return_num;
						current_bit = TinyCode::Decoding::ReadLEBUnsigned(&return_num, leb_width, current_bit, bytes);
						wasm.dylinkSection->tableSize = return_num;
						current_bit = TinyCode::Decoding::ReadLEBUnsigned(&return_num, leb_width, current_bit, bytes);
						wasm.dylinkSection->tableAlignment = return_num;
					} else if constexpr(VT == ValueWritten::ConstV128) {
						std::array<uint8_t, 16> v128_bytes;
						for(size_t i = 0; i < 16; ++i) {
							current_bit   = TinyCode::Decoding::ReadNumUnsigned(&return_num, 8, current_bit, bytes);
							v128_bytes[i] = return_num;
						}
						return wasm::Literal(v128_bytes.data());
					} else if constexpr(VT == ValueWritten::SIMDIndex) {
						// Guaranteed to be 5 bits or less
						current_bit = TinyCode::Decoding::ReadNumUnsigned(&return_num, 5, current_bit, bytes);
						return return_num;
					} else {
						std::cout << ValueWrittenName[(int)VT] << " was accidentally ignored" << std::endl;
					}
				}
			}

			void readWritingSchemes() {
				current_bit          = TinyCode::Decoding::ReadLEBUnsigned(&wasm_size, leb_width, current_bit, bytes);
				original_current_bit = current_bit;
				huffman_root         = new TinyCode::Tree::Node();
				current_bit          = TinyCode::Decoding::ReadHuffmanHeader(huffman_root, current_bit, bytes);
			}

			Name getInlineString() {
				int64_t len;
				current_bit = TinyCode::Decoding::ReadLEBUnsigned(&len, leb_width, current_bit, bytes);
				auto data   = getByteView(len);
				return Name(std::string(data.data(), data.size()));
			}

			void getResizableLimits(
				Address& initial, Address& max, bool& shared, Type& indexType, Address defaultIfNoMax) {
				int64_t flags;
				current_bit = TinyCode::Decoding::ReadNumUnsigned(&flags, 3, current_bit, bytes);
				bool is64   = (flags & BinaryConsts::Is64) != 0;

				int64_t initial_num;
				current_bit = TinyCode::Decoding::ReadLEBUnsigned(&initial_num, leb_width, current_bit, bytes);
				initial     = initial_num;

				shared    = (flags & BinaryConsts::IsShared) != 0;
				indexType = is64 ? Type::i64 : Type::i32;
				if((flags & BinaryConsts::HasMaximum) != 0) {
					int64_t max_num;
					current_bit = TinyCode::Decoding::ReadLEBUnsigned(&max_num, leb_width, current_bit, bytes);
					max         = max_num;
				} else {
					max = defaultIfNoMax;
				}
			}

			std::vector<char> getByteView(size_t size) {
				std::vector<char> byte_view;
				int64_t num;
				for(size_t i = 0; i < size; i++) {
					current_bit = TinyCode::Decoding::ReadNum(&num, 8, current_bit, bytes);
					byte_view.push_back(num);
				}
				return byte_view;
			}

			bool more() {
				return current_bit - original_current_bit < wasm_size;
			}

			char peekASTNode() {
				int64_t num;
				current_bit = TinyCode::Decoding::ReadHuffmanValue(huffman_root, &num, current_bit, bytes);
				return num;
			}

			uint64_t getPos() {
				return current_bit - original_current_bit;
			}

			void setPos(size_t newPos) {
				current_bit = newPos + original_current_bit;
			}

			size_t getSize() {
				return wasm_size;
			}

		private:
			std::vector<uint8_t>& bytes;
			uint64_t original_current_bit;
			uint64_t current_bit;
			int64_t wasm_size;
			Module& wasm;
			TinyCode::Tree::Node* huffman_root;
		};
	}
}