#include <tinycode/wasm/parser.hpp>

#include <memory>
#include <wasm-binary.h>

namespace TinyCode {
	namespace Wasm {
		void IO::WriteLEB(int64_t num) {
			bool negative = (num < 0);
			while(true) {
				// Prevent buffer overflow
				if(i < bytes.size()) {
					uint8_t b = num & 0x7F;
					// Ensure an arithmetic shift
					num >>= 7;
					if(negative) {
						num |= (~0ULL << 57);
					}
					if(((num == 0) && (!(b & 0x40))) || ((num == -1) && (b & 0x40))) {
						bytes[i++] = b;
						return;
					} else {
						bytes[i++] = b | 0x80;
					}
				} else {
					return;
				}
			}
		}

		void IO::WriteULEB(uint64_t num) {
			do {
				// Prevent buffer overflow
				if(i < bytes.size()) {
					uint8_t b = num & 0x7F;
					num >>= 7;
					if(num != 0) {
						b |= 0x80;
					}
					bytes[i++] = b;
				} else {
					return;
				}
			} while(num != 0);
			return;
		}

		int64_t IO::ReadLEB() {
			int64_t res  = 0;
			size_t shift = 0;
			while(true) {
				if(i < bytes.size()) {
					uint8_t b      = bytes[i++];
					uint64_t slice = b & 0x7F;
					res |= slice << shift;
					shift += 7;
					if(!(b & 0x80)) {
						if((shift < 64) && (b & 0x40)) {
							return res | (-1ULL) << shift;
						}
						return res;
					}
				} else {
					return 0;
				}
			}
		}

		uint64_t IO::ReadULEB() {
			uint64_t res = 0, shift = 0;
			while(true) {
				// Prevent buffer overflow
				if(i < bytes.size()) {
					uint8_t b = bytes[i++];
					res |= (b & 0x7F) << shift;
					if(!(b & 0x80)) {
						break;
					}
					shift += 7;
				} else {
					return 0;
				}
			}
			return res;
		}

		void IO::WriteU8(uint8_t num) {
			bytes[i++] = num;
		}

		uint8_t IO::ReadU8() {
			return bytes[i++];
		}

		uint8_t IO::PeekU8() {
			return bytes[i];
		}

		uint8_t IO::LastU8() {
			return bytes[i - 1];
		}

		void IO::WriteU32(uint32_t num) {
			*(uint32_t*)&bytes[i] = num;
			i += 4;
		}

		uint32_t IO::ReadU32() {
			uint32_t ret = *(uint32_t*)&bytes[i];
			i += 4;
			return ret;
		}

		void IO::WriteFloat32(float num) {
			*(float*)&bytes[i] = num;
			i += 4;
		}

		float IO::ReadFloat32() {
			float ret = *(float*)&bytes[i];
			i += 4;
			return ret;
		}

		void IO::WriteFloat64(double num) {
			*(double*)&bytes[i] = num;
			i += 8;
		}

		double IO::ReadFloat64() {
			double ret = *(double*)&bytes[i];
			i += 8;
			return ret;
		}

		bool IO::Done() {
			return i == bytes.size();
		}

		void IO::Skip(size_t len) {
			i += len;
		}

		size_t IO::GetPos() {
			return i;
		}

		void IO::WriteSlice(std::vector<uint8_t> slice) {
			std::move(slice.data(), slice.data() + slice.size(), &bytes[i]);
			i += slice.size();
		}

		void IO::WriteString(std::string str) {
			WriteULEB(str.size());
			std::move(str.data(), str.data() + str.size(), &bytes[i]);
			i += str.size();
		}

		std::vector<uint8_t> IO::ReadSlice(size_t len) {
			std::vector<uint8_t> res;
			std::copy(bytes.data() + i, bytes.data() + i + len, std::back_inserter(res));
			i += len;
			return res;
		}

		std::string IO::ReadString() {
			size_t size = ReadULEB();
			std::string res((char*)&bytes[i], size);
			res.push_back('\0');
			i += size;
			return res;
		}

		enum WasmItemType {
			NUM,           // Number of something
			SIZE,          // Size of something
			SECTION,       // Section id and size
			STRING,        // String of any kind
			TYPE,          // Type, if negative refers to value types
			INDEXED_TYPE,  // Type, only indexed
			LIMIT,         // Limit
			MEMORY_OP,     // For memory related operations
			INSTRUCTION,   // Instruction
			INSTRUCTION32, // Additional instructions
			ATTRIBUTE,     // Attribute / Mutability
			BREAK,         // Break offset, used in switch
			FUNCTION,      // Function index
			TABLE,         // Table index
			LOCAL,         // Local index
			GLOBAL,        // Global index
			TAG,           // Tag index
			I32,           // Literal
			I64,           // Literal
			I128,          // Literal
			F32,           // Literal
			F64,           // Literal
			ATOMIC_ORDER,  // Atomic order
			SEGMENT,       // Data segment
			MEMORY,        //  Memory index, must be 0 in current version of wasm
			LANE,          // SIMD lane index
			STRUCT,        // Struct index
			EXTERNAL,      // Kind of external
			FLAGS,         // Used in some places
			DATA,          // Includes segments and user data
		};

		struct WasmItem {
			WasmItemType type;
		};

		struct WasmLimit : public WasmItem {
			uint8_t flags;
			uint64_t minimum = 0;
			uint64_t maximum = 0;
		};

		struct WasmType : public WasmItem {
			int32_t type;
		};

		struct WasmIndexedType : public WasmItem {
			uint32_t type;
		};

		struct WasmMemoryOp : public WasmItem {
			uint64_t align;
			uint64_t offset;
		};

		struct WasmInstruction : public WasmItem {
			uint8_t node;
		};

		struct WasmInstruction32 : public WasmItem {
			uint32_t node;
		};

		struct WasmAttribute : public WasmItem {
			uint8_t attribute;
		};

		struct WasmBreak : public WasmItem {
			uint32_t offset;
		};

		struct WasmNumber : public WasmItem {
			uint32_t num;
		};

		struct WasmSize : public WasmItem {
			uint32_t size;
		};

		struct WasmSection : public WasmItem {
			uint8_t id;
			uint64_t size;
		};

		struct WasmString : public WasmItem {
			std::string str;
		};

		struct WasmIndex : public WasmItem {
			uint32_t index;
		};

		struct WasmI32 : public WasmItem {
			int32_t literal;
		};

		struct WasmI64 : public WasmItem {
			int64_t literal;
		};

		struct WasmI128 : public WasmItem {
			uint64_t lower;
			uint64_t upper;
		};

		struct WasmF32 : public WasmItem {
			float literal;
		};

		struct WasmF64 : public WasmItem {
			double literal;
		};

		struct WasmAtomicOrder : public WasmItem {
			uint8_t order;
		};

		struct WasmSegment : public WasmItem {
			uint32_t segment;
		};

		struct WasmMemory : public WasmItem {
			uint8_t idx;
		};

		struct WasmLane : public WasmItem {
			uint8_t lane;
		};

		struct WasmExternal : public WasmItem {
			uint8_t external;
		};

		struct WasmFlags : public WasmItem {
			uint8_t flags;
		};

		struct WasmData : public WasmItem {
			std::vector<uint8_t> data;
		};

		uint64_t WasmToOptimized(std::vector<uint8_t> wasm_bytes, uint64_t current_bit, std::vector<uint8_t> bytes) {
			IO io(wasm_bytes);
			uint32_t magic   = io.ReadU32();
			uint32_t version = io.ReadU32();

			std::vector<WasmItem*> items;

			struct Limits {
				uint64_t minimum;
				uint64_t maximum;
			};
			auto ReadLimits = [&]() {
				uint8_t flags    = io.ReadU8();
				uint64_t minimum = 0;
				uint64_t maximum = 0;
				if(flags == 0) {
					minimum = io.ReadULEB();
				} else if(flags == 1) {
					minimum = io.ReadULEB();
					maximum = io.ReadULEB();
				}
				items.push_back(new WasmLimit { LIMIT, flags, minimum, maximum });
				return Limits { minimum, maximum };
			};

			auto ReadValType = [&]() {
				int32_t type = io.ReadLEB();
				items.push_back(new WasmType { TYPE, type });
				return type;
			};

			auto ReadTable = [&]() {
				int32_t type  = ReadValType();
				Limits limits = ReadLimits();
			};

			auto ReadGlobal = [&]() {
				int32_t type       = ReadValType();
				uint8_t mutability = io.ReadU8();
				items.push_back(new WasmAttribute { ATTRIBUTE, mutability });
			};

			auto ReadMemoryOp = [&]() {
				uint64_t align  = io.ReadULEB();
				uint64_t offset = io.ReadULEB();
				items.push_back(new WasmMemoryOp { MEMORY_OP, align, offset });
			};

			std::function<void()> ReadInstructions = [&]() {
				while(true) {
					uint8_t code = io.ReadU8();
					items.push_back(new WasmInstruction { INSTRUCTION, code });
					if(code == wasm::BinaryConsts::End || code == wasm::BinaryConsts::Else) {
						return;
					} else {
						switch(code) {
						case wasm::BinaryConsts::Block:
						case wasm::BinaryConsts::Loop:
							ReadValType();
							ReadInstructions();
							break;
						case wasm::BinaryConsts::If:
							ReadValType();
							ReadInstructions();
							if(io.LastU8() == wasm::BinaryConsts::Else) {
								ReadInstructions();
							}
							break;
						case wasm::BinaryConsts::Br:
						case wasm::BinaryConsts::BrIf: {
							uint32_t break_offset = io.ReadULEB();
							items.push_back(new WasmBreak { BREAK, break_offset });
						} break;
						case wasm::BinaryConsts::BrTable: {
							uint32_t num_break_offsets = io.ReadULEB();
							items.push_back(new WasmNumber { NUM, num_break_offsets });
							for(int i = 0; i < num_break_offsets; i++) {
								uint32_t break_offset = io.ReadULEB();
								items.push_back(new WasmBreak { BREAK, break_offset });
							}
							uint32_t default_break_offset = io.ReadULEB();
							items.push_back(new WasmBreak { BREAK, default_break_offset });
						} break;
						case wasm::BinaryConsts::CallFunction: {
							uint32_t func_idx = io.ReadULEB();
							items.push_back(new WasmIndex { FUNCTION, func_idx });
						} break;
						case wasm::BinaryConsts::CallIndirect: {
							uint32_t indexed_type = io.ReadULEB();
							items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
							uint32_t table_idx = io.ReadULEB();
							items.push_back(new WasmIndex { TABLE, table_idx });
						} break;
						case wasm::BinaryConsts::SelectWithType: {
							uint32_t num_types = io.ReadULEB();
							items.push_back(new WasmNumber { NUM, num_types });
							for(int i = 0; i < num_types; i++) {
								ReadValType();
							}
						} break;
						case wasm::BinaryConsts::LocalGet:
						case wasm::BinaryConsts::LocalSet:
						case wasm::BinaryConsts::LocalTee: {
							uint32_t local_idx = io.ReadULEB();
							items.push_back(new WasmIndex { LOCAL, local_idx });
						} break;
						case wasm::BinaryConsts::GlobalGet:
						case wasm::BinaryConsts::GlobalSet: {
							uint32_t global_idx = io.ReadULEB();
							items.push_back(new WasmIndex { GLOBAL, global_idx });
						} break;
						case wasm::BinaryConsts::I32LoadMem:
						case wasm::BinaryConsts::I64LoadMem:
						case wasm::BinaryConsts::F32LoadMem:
						case wasm::BinaryConsts::F64LoadMem:
						case wasm::BinaryConsts::I32LoadMem8S:
						case wasm::BinaryConsts::I32LoadMem8U:
						case wasm::BinaryConsts::I32LoadMem16S:
						case wasm::BinaryConsts::I32LoadMem16U:
						case wasm::BinaryConsts::I64LoadMem8S:
						case wasm::BinaryConsts::I64LoadMem8U:
						case wasm::BinaryConsts::I64LoadMem16S:
						case wasm::BinaryConsts::I64LoadMem16U:
						case wasm::BinaryConsts::I64LoadMem32S:
						case wasm::BinaryConsts::I64LoadMem32U:
						case wasm::BinaryConsts::I32StoreMem:
						case wasm::BinaryConsts::I64StoreMem:
						case wasm::BinaryConsts::F32StoreMem:
						case wasm::BinaryConsts::F64StoreMem:
						case wasm::BinaryConsts::I32StoreMem8:
						case wasm::BinaryConsts::I32StoreMem16:
						case wasm::BinaryConsts::I64StoreMem8:
						case wasm::BinaryConsts::I64StoreMem16:
						case wasm::BinaryConsts::I64StoreMem32:
							ReadMemoryOp();
							break;
						case wasm::BinaryConsts::MemorySize:
						case wasm::BinaryConsts::MemoryGrow: {
							uint8_t attribute = io.ReadU8();
							items.push_back(new WasmAttribute { ATTRIBUTE, attribute });
						} break;
						case wasm::BinaryConsts::I32Const: {
							int32_t literal = io.ReadLEB();
							items.push_back(new WasmI32 { I32, literal });
						} break;
						case wasm::BinaryConsts::I64Const: {
							int64_t literal = io.ReadLEB();
							items.push_back(new WasmI64 { I64, literal });
						} break;
						case wasm::BinaryConsts::F32Const: {
							float literal = io.ReadFloat32();
							items.push_back(new WasmF32 { F32, literal });
						} break;
						case wasm::BinaryConsts::F64Const: {
							double literal = io.ReadFloat64();
							items.push_back(new WasmF64 { F64, literal });
						} break;
						case wasm::BinaryConsts::RefNull:
							ReadValType();
							break;
						case wasm::BinaryConsts::RefFunc: {
							uint32_t func_idx = io.ReadULEB();
							items.push_back(new WasmIndex { FUNCTION, func_idx });
						} break;
						case wasm::BinaryConsts::AtomicPrefix: {
							uint32_t code2 = io.ReadULEB();
							items.push_back(new WasmInstruction32 { INSTRUCTION32, code2 });

							switch(code2) {
							case wasm::BinaryConsts::I32AtomicLoad8U:
							case wasm::BinaryConsts::I32AtomicLoad16U:
							case wasm::BinaryConsts::I32AtomicLoad:
							case wasm::BinaryConsts::I64AtomicLoad8U:
							case wasm::BinaryConsts::I64AtomicLoad16U:
							case wasm::BinaryConsts::I64AtomicLoad32U:
							case wasm::BinaryConsts::I64AtomicLoad:
							case wasm::BinaryConsts::I32AtomicStore8:
							case wasm::BinaryConsts::I32AtomicStore16:
							case wasm::BinaryConsts::I32AtomicStore:
							case wasm::BinaryConsts::I64AtomicStore8:
							case wasm::BinaryConsts::I64AtomicStore16:
							case wasm::BinaryConsts::I64AtomicStore32:
							case wasm::BinaryConsts::I64AtomicStore:
							case wasm::BinaryConsts::I32AtomicWait:
							case wasm::BinaryConsts::I64AtomicWait:
							case wasm::BinaryConsts::AtomicNotify:
								ReadMemoryOp();
								break;
							case wasm::BinaryConsts::AtomicFence:
								uint8_t order = io.ReadULEB();
								items.push_back(new WasmAtomicOrder { ATOMIC_ORDER, order });
								break;
							}

							if(code2 > wasm::BinaryConsts::AtomicRMWOps_Begin
								&& code2 < wasm::BinaryConsts::AtomicRMWOps_End) {
								// Includes a range of atomic RMW instructions
								ReadMemoryOp();
							}

							if(code2 > wasm::BinaryConsts::AtomicCmpxchgOps_Begin
								&& code2 < wasm::BinaryConsts::AtomicCmpxchgOps_End) {
								// Includes a range of CMP instructions
								ReadMemoryOp();
							}
						} break;
						case wasm::BinaryConsts::MiscPrefix: {
							uint32_t code2 = io.ReadULEB();
							items.push_back(new WasmInstruction32 { INSTRUCTION32, code2 });

							switch(code2) {
							case wasm::BinaryConsts::MemoryInit:
							case wasm::BinaryConsts::DataDrop: {
								uint32_t segment_idx = io.ReadULEB();
								items.push_back(new WasmSegment { SEGMENT, segment_idx });
							} break;
							case wasm::BinaryConsts::MemoryCopy: {
								uint8_t memory_src_idx = io.ReadU8();
								items.push_back(new WasmMemory { MEMORY, memory_src_idx });
								uint8_t memory_dest_idx = io.ReadU8();
								items.push_back(new WasmMemory { MEMORY, memory_dest_idx });
							} break;
							case wasm::BinaryConsts::MemoryFill: {
								uint8_t memory_idx = io.ReadU8();
								items.push_back(new WasmMemory { MEMORY, memory_idx });
							} break;
							case wasm::BinaryConsts::TableSize:
							case wasm::BinaryConsts::TableGrow: {
								uint32_t table_idx = io.ReadULEB();
								items.push_back(new WasmIndex { TABLE, table_idx });
							} break;
							}
						} break;
						case wasm::BinaryConsts::SIMDPrefix: {
							uint32_t code2 = io.ReadULEB();
							items.push_back(new WasmInstruction32 { INSTRUCTION32, code2 });

							switch(code2) {
							case wasm::BinaryConsts::V128Const: {
								uint64_t lower = io.ReadU8() | (io.ReadU8() << 8) | (io.ReadU8() << 16)
												 | (io.ReadU8() << 24) | (io.ReadU8() << 32) | (io.ReadU8() << 40)
												 | (io.ReadU8() << 48) | (io.ReadU8() << 56);
								uint64_t upper = io.ReadU8() | (io.ReadU8() << 8) | (io.ReadU8() << 16)
												 | (io.ReadU8() << 24) | (io.ReadU8() << 32) | (io.ReadU8() << 40)
												 | (io.ReadU8() << 48) | (io.ReadU8() << 56);
								items.push_back(new WasmI128 { I128, lower, upper });
							} break;
							case wasm::BinaryConsts::V128Store:
							case wasm::BinaryConsts::V128Load:
								ReadMemoryOp();
								break;
							case wasm::BinaryConsts::I8x16ExtractLaneS:
							case wasm::BinaryConsts::I8x16ExtractLaneU:
							case wasm::BinaryConsts::I16x8ExtractLaneS:
							case wasm::BinaryConsts::I16x8ExtractLaneU:
							case wasm::BinaryConsts::I32x4ExtractLane:
							case wasm::BinaryConsts::I64x2ExtractLane:
							case wasm::BinaryConsts::F32x4ExtractLane:
							case wasm::BinaryConsts::F64x2ExtractLane:
							case wasm::BinaryConsts::I8x16ReplaceLane:
							case wasm::BinaryConsts::I16x8ReplaceLane:
							case wasm::BinaryConsts::I32x4ReplaceLane:
							case wasm::BinaryConsts::I64x2ReplaceLane:
							case wasm::BinaryConsts::F32x4ReplaceLane:
							case wasm::BinaryConsts::F64x2ReplaceLane:
							case wasm::BinaryConsts::I8x16Shuffle: {
								uint8_t lane = io.ReadU8();
								items.push_back(new WasmLane { LANE, lane });
							} break;
							case wasm::BinaryConsts::V128Load8Lane:
							case wasm::BinaryConsts::V128Load16Lane:
							case wasm::BinaryConsts::V128Load32Lane:
							case wasm::BinaryConsts::V128Load64Lane:
							case wasm::BinaryConsts::V128Store8Lane:
							case wasm::BinaryConsts::V128Store16Lane:
							case wasm::BinaryConsts::V128Store32Lane:
							case wasm::BinaryConsts::V128Store64Lane: {
								ReadMemoryOp();
								uint8_t lane = io.ReadU8();
								items.push_back(new WasmLane { LANE, lane });
							} break;
							}
						} break;
						case wasm::BinaryConsts::GCPrefix: {
							uint32_t code2 = io.ReadULEB();
							items.push_back(new WasmInstruction32 { INSTRUCTION32, code2 });

							switch(code2) {
							case wasm::BinaryConsts::RefTestStatic:
							case wasm::BinaryConsts::RefCastStatic:
							case wasm::BinaryConsts::RefCastNopStatic:
							case wasm::BinaryConsts::RttCanon:
							case wasm::BinaryConsts::RttSub:
							case wasm::BinaryConsts::RttFreshSub:
							case wasm::BinaryConsts::StructNew:
							case wasm::BinaryConsts::StructNewDefault:
							case wasm::BinaryConsts::StructNewWithRtt:
							case wasm::BinaryConsts::StructNewDefaultWithRtt:
							case wasm::BinaryConsts::ArrayNew:
							case wasm::BinaryConsts::ArrayNewDefault:
							case wasm::BinaryConsts::ArrayNewWithRtt:
							case wasm::BinaryConsts::ArrayNewDefaultWithRtt:
							case wasm::BinaryConsts::ArrayGet:
							case wasm::BinaryConsts::ArrayGetU:
							case wasm::BinaryConsts::ArrayGetS:
							case wasm::BinaryConsts::ArraySet:
							case wasm::BinaryConsts::ArrayLen: {
								uint32_t indexed_type = io.ReadULEB();
								items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
							}
							case wasm::BinaryConsts::BrOnNull:
							case wasm::BinaryConsts::BrOnNonNull:
							case wasm::BinaryConsts::BrOnCast:
							case wasm::BinaryConsts::BrOnCastFail:
							case wasm::BinaryConsts::BrOnFunc:
							case wasm::BinaryConsts::BrOnNonFunc:
							case wasm::BinaryConsts::BrOnData:
							case wasm::BinaryConsts::BrOnNonData:
							case wasm::BinaryConsts::BrOnI31:
							case wasm::BinaryConsts::BrOnNonI31: {
								uint32_t break_offset = io.ReadULEB();
								items.push_back(new WasmBreak { BREAK, break_offset });
							} break;
							case wasm::BinaryConsts::BrOnCastStatic:
							case wasm::BinaryConsts::BrOnCastStaticFail: {
								uint32_t break_offset = io.ReadULEB();
								items.push_back(new WasmBreak { BREAK, break_offset });
								uint32_t indexed_type = io.ReadULEB();
								items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
							} break;
							case wasm::BinaryConsts::StructGet:
							case wasm::BinaryConsts::StructGetS:
							case wasm::BinaryConsts::StructGetU:
							case wasm::BinaryConsts::StructSet: {
								uint32_t indexed_type = io.ReadULEB();
								items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
								uint32_t struct_idx = io.ReadULEB();
								items.push_back(new WasmIndex { STRUCT, struct_idx });
							} break;
							case wasm::BinaryConsts::ArrayInitStatic:
							case wasm::BinaryConsts::ArrayInit: {
								uint32_t indexed_type = io.ReadULEB();
								items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
								uint32_t size = io.ReadULEB();
								items.push_back(new WasmSize { SIZE, size });
							} break;
							case wasm::BinaryConsts::ArrayCopy: {
								uint32_t indexed_type_dest = io.ReadULEB();
								items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type_dest });
								uint32_t indexed_type_src = io.ReadULEB();
								items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type_src });
							} break;
							}
						} break;
						}
					}
				}
			};

			while(!io.Done()) {
				uint8_t section_id = io.ReadU8();
				size_t section_len = io.ReadULEB();
				items.push_back(new WasmSection { SECTION, section_id, section_len });
				switch(section_id) {
				case wasm::BinaryConsts::Section::User: {
					// Ignore user section
					io.Skip(section_len);
					break;
				}
				case wasm::BinaryConsts::Section::Type: {
					uint32_t num_types = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_types });
					for(uint32_t i = 0; i < num_types; i++) {
						int32_t type = io.ReadLEB();
						items.push_back(new WasmType { TYPE, type });

						if(type == wasm::BinaryConsts::EncodedType::Func) {
							uint32_t num_params = io.ReadULEB();
							items.push_back(new WasmNumber { NUM, num_params });
							for(uint32_t j = 0; j < num_params; j++) {
								int32_t param_type = io.ReadLEB();
								items.push_back(new WasmType { TYPE, param_type });
							}

							uint32_t num_results = io.ReadULEB();
							items.push_back(new WasmNumber { NUM, num_results });
							for(uint32_t j = 0; j < num_results; j++) {
								int32_t result_type = io.ReadLEB();
								items.push_back(new WasmType { TYPE, result_type });
							}
						}
					}
					break;
				}
				case wasm::BinaryConsts::Section::Import: {
					uint32_t num_imports = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_imports });
					for(uint32_t i = 0; i < num_imports; i++) {
						std::string module = io.ReadString();
						items.push_back(new WasmString { STRING, module });
						std::string name = io.ReadString();
						items.push_back(new WasmString { STRING, name });
						uint8_t import_type = io.ReadU8();
						items.push_back(new WasmExternal { EXTERNAL, import_type });

						switch((wasm::ExternalKind)import_type) {
						case wasm::ExternalKind::Function: {
							uint32_t indexed_type = io.ReadULEB();
							items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
						} break;
						case wasm::ExternalKind::Table: {
							ReadTable();
						} break;
						case wasm::ExternalKind::Memory: {
							ReadLimits();
						} break;
						case wasm::ExternalKind::Global: {
							ReadGlobal();
						} break;
						case wasm::ExternalKind::Tag: {
							uint32_t indexed_type = io.ReadULEB();
							items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
						} break;
						}
					}
					break;
				}
				case wasm::BinaryConsts::Section::Function: {
					uint32_t num_funcs = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_funcs });
					for(uint32_t i = 0; i < num_funcs; i++) {
						uint32_t indexed_type = io.ReadULEB();
						items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
					}
					break;
				}
				case wasm::BinaryConsts::Section::Table: {
					uint32_t num_tables = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_tables });
					for(uint32_t i = 0; i < num_tables; i++) {
						ReadTable();
					}
					break;
				}
				case wasm::BinaryConsts::Section::Memory: {
					uint32_t num_mems = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_mems });
					for(uint32_t i = 0; i < num_mems; i++) {
						ReadLimits();
					}
					break;
				}
				case wasm::BinaryConsts::Section::Global: {
					uint32_t num_globals = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_globals });
					for(uint32_t i = 0; i < num_globals; i++) {
						ReadGlobal();
						ReadInstructions();
					}
					break;
				}
				case wasm::BinaryConsts::Section::Export: {
					uint32_t num_exports = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_exports });
					for(uint32_t i = 0; i < num_exports; i++) {
						std::string name = io.ReadString();
						items.push_back(new WasmString { STRING, name });
						uint8_t export_type = io.ReadU8();
						items.push_back(new WasmExternal { EXTERNAL, export_type });

						uint32_t idx = io.ReadULEB();
						switch((wasm::ExternalKind)export_type) {
						case wasm::ExternalKind::Function: {
							items.push_back(new WasmIndex { FUNCTION, idx });
						} break;
						case wasm::ExternalKind::Table: {
							items.push_back(new WasmIndex { TABLE, idx });
						} break;
						case wasm::ExternalKind::Memory: {
							items.push_back(new WasmIndex { MEMORY, idx });
						} break;
						case wasm::ExternalKind::Global: {
							items.push_back(new WasmIndex { GLOBAL, idx });
						} break;
						case wasm::ExternalKind::Tag: {
							items.push_back(new WasmIndex { TAG, idx });
						} break;
						}
					}
					break;
				}
				case wasm::BinaryConsts::Section::Start: {
					uint32_t func_idx = io.ReadULEB();
					items.push_back(new WasmIndex { FUNCTION, func_idx });
					break;
				}
				case wasm::BinaryConsts::Section::Element: {
					uint32_t num_elements = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_elements });
					for(uint32_t i = 0; i < num_elements; i++) {
						uint8_t flags = io.ReadU8();
						items.push_back(new WasmFlags { FLAGS, flags });
						if(flags == 0) {
							ReadInstructions();
							uint32_t num_funcs = io.ReadULEB();
							for(int j = 0; j < num_funcs; j++) {
								uint32_t func_idx = io.ReadULEB();
								items.push_back(new WasmIndex { FUNCTION, func_idx });
							}
						}
					}
					break;
				}
				case wasm::BinaryConsts::Section::Code: {
					uint32_t num_funcs = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_funcs });
					for(uint32_t i = 0; i < num_funcs; i++) {
						uint32_t size = io.ReadULEB();
						items.push_back(new WasmSize { SIZE, size });
						uint32_t num_local_types = io.ReadULEB();
						items.push_back(new WasmNumber { NUM, num_local_types });
						for(int j = 0; j < num_local_types; j++) {
							uint32_t num_locals = io.ReadULEB();
							items.push_back(new WasmNumber { NUM, num_locals });
							ReadValType();
						}

						ReadInstructions();
					}
					break;
				}
				case wasm::BinaryConsts::Section::Data: {
					uint32_t num_segments = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_segments });
					for(uint32_t i = 0; i < num_segments; i++) {
						uint8_t flags = io.ReadU8();
						items.push_back(new WasmFlags { FLAGS, flags });

						if(flags == 0) {
							ReadInstructions();
						}

						uint32_t size = io.ReadULEB();
						items.push_back(new WasmSize { SIZE, size });
						items.push_back(new WasmData { DATA, io.ReadSlice(size) });
					}
					break;
				}
				case wasm::BinaryConsts::Section::DataCount: {
					uint32_t num_segments = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_segments });
				}
				case wasm::BinaryConsts::Section::Tag: {
					uint32_t num_tags = io.ReadULEB();
					items.push_back(new WasmNumber { NUM, num_tags });
					for(uint32_t i = 0; i < num_tags; i++) {
						uint8_t attribute = io.ReadU8();
						items.push_back(new WasmAttribute { ATTRIBUTE, attribute });
						uint32_t indexed_type = io.ReadULEB();
						items.push_back(new WasmIndexedType { INDEXED_TYPE, indexed_type });
					}
					break;
				}
				}
			}

			return current_bit;
		}
	}
}