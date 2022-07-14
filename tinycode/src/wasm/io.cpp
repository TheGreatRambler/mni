#include <tinycode/wasm/io.hpp>

#include "ir/eh-utils.h"
#include "ir/table-utils.h"
#include "ir/type-updating.h"

namespace TinyCode {
	namespace Wasm {
		//#define BYN_TRACE(MSG) std::cout << MSG;

		void OptimizedWasmWriter::prepare() {
			// Collect function types and their frequencies. Collect information in each
			// function in parallel, then merge.
			indexedTypes = ModuleUtils::getOptimizedIndexedHeapTypes(*wasm);
			importInfo   = wasm::make_unique<ImportInfo>(*wasm);
		}

		uint64_t OptimizedWasmWriter::write() {
			writeHeader();

			writeDylinkSection();

			initializeDebugInfo();
			if(sourceMap) {
				writeSourceMapProlog();
			}

			writeTypes();
			writeImports();
			writeFunctionSignatures();
			writeTableDeclarations();
			writeMemory();
			writeTags();
			writeGlobals();
			writeExports();
			writeStart();
			writeElementSegments();
			writeDataCount();
			writeFunctions();
			writeDataSegments();
			if(debugInfo || emitModuleName) {
				writeNames();
			}
			if(sourceMap && !sourceMapUrl.empty()) {
				writeSourceMapUrl();
			}
			if(symbolMap.size() > 0) {
				writeSymbolMap();
			}

			if(sourceMap) {
				writeSourceMapEpilog();
			}

#ifdef BUILD_LLVM_DWARF
			// Update DWARF user sections after writing the data they refer to
			// (function bodies), and before writing the user sections themselves.
			if(Debug::hasDWARFSections(*wasm)) {
				Debug::writeDWARFSections(*wasm, binaryLocations);
			}
#endif

			writeLateUserSections();
			writeFeaturesSection();

			return binaryIO.streamOffset();
		}

		void OptimizedWasmWriter::writeHeader() {
			BYN_TRACE("== writeHeader\n");
			writeValue<ValueWritten::Magic>(); // magic number \0asm
			writeValue<ValueWritten::Version>();
		}

		int32_t OptimizedWasmWriter::writeU32LEBPlaceholder() {
			int32_t ret = binaryIO.streamOffset();
			writeValue<ValueWritten::SectionSize>();
			return ret;
		}

		template <typename T> int32_t OptimizedWasmWriter::startSection(T code) {
			writeValue<ValueWritten::SectionStart>(code);
			if(sourceMap) {
				sourceMapLocationsSizeAtSectionStart = sourceMapLocations.size();
			}
			binaryLocationsSizeAtSectionStart = binaryLocations.expressions.size();
			return writeU32LEBPlaceholder(); // section size to be filled in later
		}

		void OptimizedWasmWriter::finishSection(int32_t start) {
			// Offsets are relative to the body of the code section: after the
			// section type byte and the size.
			// Everything was moved by the adjustment, track that. After this,
			// we are at the right absolute address.
			// We are relative to the section start.
			if(!binaryIO.isHuffmanCreation()) {
				auto totalAdjustment = streamAdjustSectionLEB(start);
				if(binaryLocationsSizeAtSectionStart != binaryLocations.expressions.size()) {
					// We added the binary locations, adjust them: they must be relative
					// to the code section.
					assert(binaryLocationsSizeAtSectionStart == 0);
					for(auto& [_, locations] : binaryLocations.expressions) {
						locations.start -= totalAdjustment;
						locations.end -= totalAdjustment;
					}
					for(auto& [_, locations] : binaryLocations.functions) {
						locations.start -= totalAdjustment;
						locations.declarations -= totalAdjustment;
						locations.end -= totalAdjustment;
					}
					for(auto& [_, locations] : binaryLocations.delimiters) {
						for(auto& item : locations) {
							item -= totalAdjustment;
						}
					}
				}
			}
		}

		int32_t OptimizedWasmWriter::startSubsection(BinaryConsts::UserSections::Subsection code) {
			return startSection(code);
		}

		void OptimizedWasmWriter::finishSubsection(int32_t start) {
			finishSection(start);
		}

		void OptimizedWasmWriter::writeStart() {
			if(!wasm->start.is()) {
				return;
			}
			BYN_TRACE("== writeStart\n");
			auto start = startSection(BinaryConsts::Section::Start);
			writeValue<ValueWritten::FunctionIndex>(getFunctionIndex(wasm->start.str));
			finishSection(start);
		}

		void OptimizedWasmWriter::writeMemory() {
			if(!wasm->memory.exists || wasm->memory.imported()) {
				return;
			}
			BYN_TRACE("== writeMemory\n");
			auto start = startSection(BinaryConsts::Section::Memory);
			writeValue<ValueWritten::CountMemories>(1); // Define 1 memory
			binaryIO.writeResizableLimits(wasm->memory.initial, wasm->memory.max, wasm->memory.hasMax(),
				wasm->memory.shared, wasm->memory.is64());
			finishSection(start);
		}

		void OptimizedWasmWriter::writeTypes() {
			if(indexedTypes.types.size() == 0) {
				return;
			}
			// Count the number of recursion groups, which is the number of elements in
			// the type section. With nominal typing there is always one group and with
			// equirecursive typing there is one group per type.
			size_t numGroups = 0;
			switch(getTypeSystem()) {
			case TypeSystem::Equirecursive:
				numGroups = indexedTypes.types.size();
				break;
			case TypeSystem::Nominal:
				numGroups = 1;
				break;
			case TypeSystem::Isorecursive: {
				std::optional<RecGroup> lastGroup;
				for(auto type : indexedTypes.types) {
					auto currGroup = type.getRecGroup();
					numGroups += lastGroup != currGroup;
					lastGroup = currGroup;
				}
			}
			}
			BYN_TRACE("== writeTypes\n");
			auto start = startSection(BinaryConsts::Section::Type);
			writeValue<ValueWritten::CountTypeGroups>(numGroups);
			if(getTypeSystem() == TypeSystem::Nominal) {
				// The nominal recursion group contains every type.
				writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::Rec);
				writeValue<ValueWritten::NumTypes>(indexedTypes.types.size());
			}
			std::optional<RecGroup> lastGroup = std::nullopt;
			for(Index i = 0; i < indexedTypes.types.size(); ++i) {
				auto type = indexedTypes.types[i];
				// Check whether we need to start a new recursion group. Recursion groups of
				// size 1 are implicit, so only emit a group header for larger groups. This
				// gracefully handles non-isorecursive type systems, which only have groups
				// of size 1 internally (even though nominal types are emitted as a single
				// large group).
				auto currGroup = type.getRecGroup();
				if(lastGroup != currGroup && currGroup.size() > 1) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::Rec);
					writeValue<ValueWritten::NumTypes>(currGroup.size());
				}
				lastGroup = currGroup;
				// Emit the type definition.
				BYN_TRACE("write " << type << std::endl);
				if(auto super = type.getSuperType()) {
					// Subtype constructor and vector of 1 supertype.
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::Sub);
					// writeValue<ValueWritten::NumTypes>(1);
					writeHeapType(*super);
				}
				if(type.isSignature()) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::Func);
					auto sig = type.getSignature();
					for(auto& sigType : { sig.params, sig.results }) {
						writeValue<ValueWritten::NumFunctionParams>(sigType.size());
						for(const auto& type : sigType) {
							writeType(type);
						}
					}
				} else if(type.isStruct()) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::Struct);
					auto fields = type.getStruct().fields;
					writeValue<ValueWritten::NumStructFields>(fields.size());
					for(const auto& field : fields) {
						writeField(field);
					}
				} else if(type.isArray()) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::Array);
					writeField(type.getArray().element);
				} else {
					WASM_UNREACHABLE("TODO GC type writing");
				}
			}
			finishSection(start);
		}

		void OptimizedWasmWriter::writeImports() {
			auto num = importInfo->getNumImports();
			if(num == 0) {
				return;
			}
			BYN_TRACE("== writeImports\n");
			auto start = startSection(BinaryConsts::Section::Import);
			writeValue<ValueWritten::CountImports>(num);
			auto writeImportHeader = [&](Importable* import) {
				binaryIO.writeInlineString(import->module.str);
				binaryIO.writeInlineString(import->base.str);
			};
			ModuleUtils::iterImportedFunctions(*wasm, [&](Function* func) {
				BYN_TRACE("write one function\n");
				writeImportHeader(func);
				writeValue<ValueWritten::ExternalKind>(ExternalKind::Function);
				writeValue<ValueWritten::TypeIndex>(getTypeIndex(func->type));
			});
			ModuleUtils::iterImportedGlobals(*wasm, [&](Global* global) {
				BYN_TRACE("write one global\n");
				writeImportHeader(global);
				writeValue<ValueWritten::ExternalKind>(ExternalKind::Global);
				writeType(global->type);
				writeValue<ValueWritten::Mutable>(global->mutable_);
			});
			ModuleUtils::iterImportedTags(*wasm, [&](Tag* tag) {
				BYN_TRACE("write one tag\n");
				writeImportHeader(tag);
				writeValue<ValueWritten::ExternalKind>(ExternalKind::Tag);
				// Reserved 'attribute' field. Always 0.
				writeValue<ValueWritten::Attribute>(0);
				writeValue<ValueWritten::TypeIndex>(getTypeIndex(tag->sig));
			});
			if(wasm->memory.imported()) {
				BYN_TRACE("write one memory\n");
				writeImportHeader(&wasm->memory);
				writeValue<ValueWritten::ExternalKind>(ExternalKind::Memory);
				binaryIO.writeResizableLimits(wasm->memory.initial, wasm->memory.max, wasm->memory.hasMax(),
					wasm->memory.shared, wasm->memory.is64());
			}
			ModuleUtils::iterImportedTables(*wasm, [&](Table* table) {
				BYN_TRACE("write one table\n");
				writeImportHeader(table);
				writeValue<ValueWritten::ExternalKind>(ExternalKind::Table);
				writeType(table->type);
				binaryIO.writeResizableLimits(table->initial, table->max, table->hasMax(),
					/*shared=*/false,
					/*is64*/ false);
			});
			finishSection(start);
		}

		void OptimizedWasmWriter::writeFunctionSignatures() {
			if(importInfo->getNumDefinedFunctions() == 0) {
				return;
			}
			BYN_TRACE("== writeFunctionSignatures\n");
			auto start = startSection(BinaryConsts::Section::Function);
			writeValue<ValueWritten::CountDefinedFunctions>(importInfo->getNumDefinedFunctions());
			ModuleUtils::iterDefinedFunctions(*wasm, [&](Function* func) {
				BYN_TRACE("write one\n");
				writeValue<ValueWritten::TypeIndex>(getTypeIndex(func->type));
			});
			finishSection(start);
		}

		void OptimizedWasmWriter::writeExpression(Expression* curr) {
			NewBinaryenIRToBinaryWriter(*this).visit(curr);
		}

		void OptimizedWasmWriter::writeFunctions() {
			if(importInfo->getNumDefinedFunctions() == 0) {
				return;
			}
			BYN_TRACE("== writeFunctions\n");
			auto sectionStart = startSection(BinaryConsts::Section::Code);
			writeValue<ValueWritten::CountDefinedFunctions>(importInfo->getNumDefinedFunctions());
			bool DWARF = wasm::Debug::hasDWARFSections(*getModule());
			ModuleUtils::iterDefinedFunctions(*wasm, [&](Function* func) {
				assert(binaryLocationTrackedExpressionsForFunc.empty());
				size_t sourceMapLocationsSizeAtFunctionStart = sourceMapLocations.size();
				BYN_TRACE("write one at" << binaryIO.streamOffset() << std::endl);
				size_t sizePos = writeU32LEBPlaceholder();
				size_t start   = binaryIO.streamOffset();
				BYN_TRACE("writing" << func->name << std::endl);
				// Emit Stack IR if present, and if we can
				if(func->stackIR && !sourceMap && !DWARF) {
					BYN_TRACE("write Stack IR\n");
					NewStackIRToBinaryWriter writer(*this, func);
					writer.write();
					if(debugInfo) {
						funcMappedLocals[func->name] = std::move(writer.getMappedLocals());
					}
				} else {
					BYN_TRACE("write Binaryen IR\n");
					NewBinaryenIRToBinaryWriter writer(*this, func, sourceMap, DWARF);
					writer.write();
					if(debugInfo) {
						funcMappedLocals[func->name] = std::move(writer.getMappedLocals());
					}
				}
				size_t size = binaryIO.streamOffset() - start;
				assert(size <= std::numeric_limits<uint32_t>::max());
				BYN_TRACE("body size: " << size << ", writing at " << sizePos << ", next starts at "
										<< binaryIO.streamOffset() << "\n");

				// Adjust function size LEB
				if(!binaryIO.isHuffmanCreation()) {
					streamAdjustFunctionLEB(func, start, size, sizePos, sourceMapLocationsSizeAtFunctionStart);
				}
			});
			finishSection(sectionStart);
		}

		void OptimizedWasmWriter::writeGlobals() {
			if(importInfo->getNumDefinedGlobals() == 0) {
				return;
			}
			BYN_TRACE("== writeglobals\n");
			auto start = startSection(BinaryConsts::Section::Global);
			// Count and emit the total number of globals after tuple globals have been
			// expanded into their constituent parts.
			Index num = 0;
			ModuleUtils::iterDefinedGlobals(*wasm, [&num](Global* global) { num += global->type.size(); });
			writeValue<ValueWritten::CountGlobals>(num);
			ModuleUtils::iterDefinedGlobals(*wasm, [&](Global* global) {
				BYN_TRACE("write one\n");
				size_t i = 0;
				for(const auto& t : global->type) {
					writeType(t);
					writeValue<ValueWritten::Mutable>(global->mutable_);
					if(global->type.size() == 1) {
						writeExpression(global->init);
					} else {
						writeExpression(global->init->cast<TupleMake>()->operands[i]);
					}
					writeValue<ValueWritten::ASTNode>(BinaryConsts::End);
					++i;
				}
			});
			finishSection(start);
		}

		void OptimizedWasmWriter::writeExports() {
			if(wasm->exports.size() == 0) {
				return;
			}
			BYN_TRACE("== writeexports\n");
			auto start = startSection(BinaryConsts::Section::Export);
			writeValue<ValueWritten::CountExports>(wasm->exports.size());
			for(auto& curr : wasm->exports) {
				BYN_TRACE("write one\n");
				binaryIO.writeInlineString(curr->name.str);
				writeValue<ValueWritten::ExternalKind>(curr->kind);
				switch(curr->kind) {
				case ExternalKind::Function:
					writeValue<ValueWritten::FunctionIndex>(getFunctionIndex(curr->value));
					break;
				case ExternalKind::Table:
					writeValue<ValueWritten::TableIndex>(0);
					break;
				case ExternalKind::Memory:
					writeValue<ValueWritten::MemoryIndex>(0);
					break;
				case ExternalKind::Global:
					writeValue<ValueWritten::GlobalIndex>(getGlobalIndex(curr->value));
					break;
				case ExternalKind::Tag:
					writeValue<ValueWritten::TagIndex>(getTagIndex(curr->value));
					break;
				default:
					WASM_UNREACHABLE("unexpected extern kind");
				}
			}
			finishSection(start);
		}

		void OptimizedWasmWriter::writeDataCount() {
			if(!wasm->features.hasBulkMemory() || !wasm->memory.segments.size()) {
				return;
			}
			auto start = startSection(BinaryConsts::Section::DataCount);
			writeValue<ValueWritten::CountMemorySegments>(wasm->memory.segments.size());
			finishSection(start);
		}

		void OptimizedWasmWriter::writeDataSegments() {
			if(wasm->memory.segments.size() == 0) {
				return;
			}
			if(wasm->memory.segments.size() > WebLimitations::MaxDataSegments) {
				std::cerr << "Some VMs may not accept this binary because it has a large "
						  << "number of data segments. Run the limit-segments pass to "
						  << "merge segments.\n";
			}
			auto start = startSection(BinaryConsts::Section::Data);
			writeValue<ValueWritten::CountMemorySegments>(wasm->memory.segments.size());
			for(auto& segment : wasm->memory.segments) {
				uint32_t flags = 0;
				if(segment.isPassive) {
					flags |= BinaryConsts::IsPassive;
				}
				writeValue<ValueWritten::MemorySegmentFlags>(flags);
				if(!segment.isPassive) {
					writeExpression(segment.offset);
					writeValue<ValueWritten::ASTNode>(BinaryConsts::End);
				}
				writeInlineBuffer(segment.data.data(), segment.data.size());
			}
			finishSection(start);
		}

		uint32_t OptimizedWasmWriter::getFunctionIndex(Name name) const {
			auto it = indexes.functionIndexes.find(name);
			assert(it != indexes.functionIndexes.end());
			return it->second;
		}

		uint32_t OptimizedWasmWriter::getTableIndex(Name name) const {
			auto it = indexes.tableIndexes.find(name);
			assert(it != indexes.tableIndexes.end());
			return it->second;
		}

		uint32_t OptimizedWasmWriter::getGlobalIndex(Name name) const {
			auto it = indexes.globalIndexes.find(name);
			assert(it != indexes.globalIndexes.end());
			return it->second;
		}

		uint32_t OptimizedWasmWriter::getTagIndex(Name name) const {
			auto it = indexes.tagIndexes.find(name);
			assert(it != indexes.tagIndexes.end());
			return it->second;
		}

		uint32_t OptimizedWasmWriter::getTypeIndex(HeapType type) const {
			auto it = indexedTypes.indices.find(type);
#ifndef NDEBUG
			if(it == indexedTypes.indices.end()) {
				std::cout << "Missing type: " << type << '\n';
				assert(0);
			}
#endif
			return it->second;
		}

		void OptimizedWasmWriter::writeTableDeclarations() {
			if(importInfo->getNumDefinedTables() == 0) {
				// std::cerr << std::endl << "(WasmBinaryWriter::writeTableDeclarations)
				// No defined tables found. skipping" << std::endl;
				return;
			}
			BYN_TRACE("== writeTableDeclarations\n");
			auto start = startSection(BinaryConsts::Section::Table);
			writeValue<ValueWritten::CountDefinedTables>(importInfo->getNumDefinedTables());
			ModuleUtils::iterDefinedTables(*wasm, [&](Table* table) {
				writeType(table->type);
				binaryIO.writeResizableLimits(table->initial, table->max, table->hasMax(),
					/*shared=*/false,
					/*is64*/ false);
			});
			finishSection(start);
		}

		void OptimizedWasmWriter::writeElementSegments() {
			size_t elemCount     = wasm->elementSegments.size();
			auto needingElemDecl = TableUtils::getFunctionsNeedingElemDeclare(*wasm);
			if(!needingElemDecl.empty()) {
				elemCount++;
			}
			if(elemCount == 0) {
				return;
			}

			BYN_TRACE("== writeElementSegments\n");
			auto start = startSection(BinaryConsts::Section::Element);
			writeValue<ValueWritten::CountElementSegments>(elemCount);

			for(auto& segment : wasm->elementSegments) {
				Index tableIdx = 0;

				bool isPassive = segment->table.isNull();
				// If the segment is MVP, we can use the shorter form.
				bool usesExpressions = TableUtils::usesExpressions(segment.get(), wasm);

				// The table index can and should be elided for active segments of table 0
				// when table 0 has type funcref. This was the only type of segment
				// supported by the MVP, which also did not support table indices in the
				// segment encoding.
				bool hasTableIndex = false;
				if(!isPassive) {
					tableIdx      = getTableIndex(segment->table);
					hasTableIndex = tableIdx > 0 || wasm->getTable(segment->table)->type != Type::funcref;
				}

				uint32_t flags = 0;
				if(usesExpressions) {
					flags |= BinaryConsts::UsesExpressions;
				}
				if(isPassive) {
					flags |= BinaryConsts::IsPassive;
				} else if(hasTableIndex) {
					flags |= BinaryConsts::HasIndex;
				}

				writeValue<ValueWritten::ElementSegmentFlags>(flags);
				if(!isPassive) {
					if(hasTableIndex) {
						writeValue<ValueWritten::TableIndex>(tableIdx);
					}
					writeExpression(segment->offset);
					writeValue<ValueWritten::ASTNode>(BinaryConsts::End);
				}

				if(isPassive || hasTableIndex) {
					if(usesExpressions) {
						// elemType
						writeType(segment->type);
					} else {
						// MVP elemKind of funcref
						writeValue<ValueWritten::ElementSegmentType>(0);
					}
				}
				writeValue<ValueWritten::ElementSegmentSize>(segment->data.size());
				if(usesExpressions) {
					for(auto* item : segment->data) {
						writeExpression(item);
						writeValue<ValueWritten::ASTNode>(BinaryConsts::End);
					}
				} else {
					for(auto& item : segment->data) {
						// We've ensured that all items are ref.func.
						auto& name = item->cast<RefFunc>()->func;
						writeValue<ValueWritten::FunctionIndex>(getFunctionIndex(name));
					}
				}
			}

			if(!needingElemDecl.empty()) {
				writeValue<ValueWritten::ElementSegmentFlags>(BinaryConsts::IsPassive | BinaryConsts::IsDeclarative);
				// type 0 (indicating funcref)
				writeValue<ValueWritten::ElementSegmentType>(0);
				writeValue<ValueWritten::ElementSegmentSize>(needingElemDecl.size());
				for(auto name : needingElemDecl) {
					writeValue<ValueWritten::FunctionIndex>(indexes.functionIndexes[name]);
				}
			}

			finishSection(start);
		}

		void OptimizedWasmWriter::writeTags() {
			if(importInfo->getNumDefinedTags() == 0) {
				return;
			}
			BYN_TRACE("== writeTags\n");
			auto start = startSection(BinaryConsts::Section::Tag);
			auto num   = importInfo->getNumDefinedTags();
			writeValue<ValueWritten::CountDefinedTags>(num);
			ModuleUtils::iterDefinedTags(*wasm, [&](Tag* tag) {
				BYN_TRACE("write one\n");
				writeValue<ValueWritten::Attribute>(0);
				writeValue<ValueWritten::TypeIndex>(getTypeIndex(tag->sig));
			});

			finishSection(start);
		}

		void OptimizedWasmWriter::writeNames() {
			BYN_TRACE("== writeNames\n");
			auto start = startSection(BinaryConsts::Section::User);
			binaryIO.writeInlineString(BinaryConsts::UserSections::Name);

			// module name
			if(emitModuleName && wasm->name.is()) {
				auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameModule);
				writeEscapedName(wasm->name.str);
				finishSubsection(substart);
			}

			if(!debugInfo) {
				// We were only writing the module name.
				finishSection(start);
				return;
			}

			// function names
			{
				auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameFunction);
				writeValue<ValueWritten::CountFunctionIndices>(indexes.functionIndexes.size());
				Index emitted = 0;
				auto add      = [&](Function* curr) {
                    writeValue<ValueWritten::FunctionIndex>(emitted);
                    writeEscapedName(curr->name.str);
                    emitted++;
				};
				ModuleUtils::iterImportedFunctions(*wasm, add);
				ModuleUtils::iterDefinedFunctions(*wasm, add);
				assert(emitted == indexes.functionIndexes.size());
				finishSubsection(substart);
			}

			// local names
			{
				// Find all functions with at least one local name and only emit the
				// subsection if there is at least one.
				std::vector<std::pair<Index, Function*>> functionsWithLocalNames;
				Index checked = 0;
				auto check    = [&](Function* curr) {
                    auto numLocals = curr->getNumLocals();
                    for(Index i = 0; i < numLocals; ++i) {
                        if(curr->hasLocalName(i)) {
                            functionsWithLocalNames.push_back({ checked, curr });
                            break;
                        }
                    }
                    checked++;
				};
				ModuleUtils::iterImportedFunctions(*wasm, check);
				ModuleUtils::iterDefinedFunctions(*wasm, check);
				assert(checked == indexes.functionIndexes.size());
				if(functionsWithLocalNames.size() > 0) {
					// Otherwise emit those functions but only include locals with a name.
					auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameLocal);
					writeValue<ValueWritten::CountFunctionIndices>(functionsWithLocalNames.size());
					Index emitted = 0;
					for(auto& [index, func] : functionsWithLocalNames) {
						// Pairs of (local index in IR, name).
						std::vector<std::pair<Index, Name>> localsWithNames;
						auto numLocals = func->getNumLocals();
						for(Index i = 0; i < numLocals; ++i) {
							if(func->hasLocalName(i)) {
								localsWithNames.push_back({ i, func->getLocalName(i) });
							}
						}
						assert(localsWithNames.size());
						writeValue<ValueWritten::FunctionIndex>(index);
						writeValue<ValueWritten::NumFunctionLocals>(localsWithNames.size());
						for(auto& [indexInFunc, name] : localsWithNames) {
							// TODO: handle multivalue
							Index indexInBinary;
							auto iter = funcMappedLocals.find(func->name);
							if(iter != funcMappedLocals.end()) {
								indexInBinary = iter->second[{ indexInFunc, 0 }];
							} else {
								// No data on funcMappedLocals. That is only possible if we are an
								// imported function, where there are no locals to map, and in that
								// case the index is unchanged anyhow: parameters always have the
								// same index, they are not mapped in any way.
								assert(func->imported());
								indexInBinary = indexInFunc;
							}
							writeValue<ValueWritten::LocalIndex>(indexInBinary);
							writeEscapedName(name.str);
						}
						emitted++;
					}
					assert(emitted == functionsWithLocalNames.size());
					finishSubsection(substart);
				}
			}

			// type names
			{
				std::vector<HeapType> namedTypes;
				for(auto& [type, _] : indexedTypes.indices) {
					if(wasm->typeNames.count(type) && wasm->typeNames[type].name.is()) {
						namedTypes.push_back(type);
					}
				}
				if(!namedTypes.empty()) {
					auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameType);
					writeValue<ValueWritten::CountTypeNames>(namedTypes.size());
					for(auto type : namedTypes) {
						writeValue<ValueWritten::TypeIndex>(indexedTypes.indices[type]);
						writeEscapedName(wasm->typeNames[type].name.str);
					}
					finishSubsection(substart);
				}
			}

			// table names
			{
				std::vector<std::pair<Index, Table*>> tablesWithNames;
				Index checked = 0;
				auto check    = [&](Table* curr) {
                    if(curr->hasExplicitName) {
                        tablesWithNames.push_back({ checked, curr });
                    }
                    checked++;
				};
				ModuleUtils::iterImportedTables(*wasm, check);
				ModuleUtils::iterDefinedTables(*wasm, check);
				assert(checked == indexes.tableIndexes.size());

				if(tablesWithNames.size() > 0) {
					auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameTable);
					writeValue<ValueWritten::CountTables>(tablesWithNames.size());

					for(auto& [index, table] : tablesWithNames) {
						writeValue<ValueWritten::TableIndex>(index);
						writeEscapedName(table->name.str);
					}

					finishSubsection(substart);
				}
			}

			// memory names
			if(wasm->memory.exists && wasm->memory.hasExplicitName) {
				auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameMemory);
				// currently exactly 1 memory at index 0
				writeValue<ValueWritten::CountMemories>(1);
				writeValue<ValueWritten::MemoryIndex>(0);
				writeEscapedName(wasm->memory.name.str);
				finishSubsection(substart);
			}

			// global names
			{
				std::vector<std::pair<Index, Global*>> globalsWithNames;
				Index checked = 0;
				auto check    = [&](Global* curr) {
                    if(curr->hasExplicitName) {
                        globalsWithNames.push_back({ checked, curr });
                    }
                    checked++;
				};
				ModuleUtils::iterImportedGlobals(*wasm, check);
				ModuleUtils::iterDefinedGlobals(*wasm, check);
				assert(checked == indexes.globalIndexes.size());
				if(globalsWithNames.size() > 0) {
					auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameGlobal);
					writeValue<ValueWritten::CountGlobals>(globalsWithNames.size());
					for(auto& [index, global] : globalsWithNames) {
						writeValue<ValueWritten::GlobalIndex>(index);
						writeEscapedName(global->name.str);
					}
					finishSubsection(substart);
				}
			}

			// elem segment names
			{
				std::vector<std::pair<Index, ElementSegment*>> elemsWithNames;
				Index checked = 0;
				for(auto& curr : wasm->elementSegments) {
					if(curr->hasExplicitName) {
						elemsWithNames.push_back({ checked, curr.get() });
					}
					checked++;
				}
				assert(checked == indexes.elemIndexes.size());

				if(elemsWithNames.size() > 0) {
					auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameElem);
					writeValue<ValueWritten::CountElementSegments>(elemsWithNames.size());

					for(auto& [index, elem] : elemsWithNames) {
						writeValue<ValueWritten::ElementSegmentIndex>(index);
						writeEscapedName(elem->name.str);
					}

					finishSubsection(substart);
				}
			}

			// data segment names
			if(wasm->memory.exists) {
				Index count = 0;
				for(auto& seg : wasm->memory.segments) {
					if(seg.name.is()) {
						count++;
					}
				}

				if(count) {
					auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameData);
					writeValue<ValueWritten::CountMemorySegments>(count);
					for(Index i = 0; i < wasm->memory.segments.size(); i++) {
						auto& seg = wasm->memory.segments[i];
						if(seg.name.is()) {
							writeValue<ValueWritten::MemorySegmentIndex>(i);
							writeEscapedName(seg.name.str);
						}
					}
					finishSubsection(substart);
				}
			}

			// TODO: label, type, and element names
			// see: https://github.com/WebAssembly/extended-name-section

			// GC field names
			if(wasm->features.hasGC()) {
				std::vector<HeapType> relevantTypes;
				for(auto& type : indexedTypes.types) {
					if(type.isStruct() && wasm->typeNames.count(type) && !wasm->typeNames[type].fieldNames.empty()) {
						relevantTypes.push_back(type);
					}
				}
				if(!relevantTypes.empty()) {
					auto substart = startSubsection(BinaryConsts::UserSections::Subsection::NameField);
					writeValue<ValueWritten::CountGCFieldTypes>(relevantTypes.size());
					for(Index i = 0; i < relevantTypes.size(); i++) {
						auto type = relevantTypes[i];
						writeValue<ValueWritten::TypeIndex>(indexedTypes.indices[type]);
						std::unordered_map<Index, Name>& fieldNames = wasm->typeNames.at(type).fieldNames;
						writeValue<ValueWritten::NumGCFields>(fieldNames.size());
						for(auto& [index, name] : fieldNames) {
							writeValue<ValueWritten::GCFieldIndex>(index);
							writeEscapedName(name.str);
						}
					}
					finishSubsection(substart);
				}
			}

			finishSection(start);
		}

		void OptimizedWasmWriter::writeSourceMapUrl() {
			BYN_TRACE("== writeSourceMapUrl\n");
			auto start = startSection(BinaryConsts::Section::User);
			binaryIO.writeInlineString(BinaryConsts::UserSections::SourceMapUrl);
			binaryIO.writeInlineString(sourceMapUrl.c_str());
			finishSection(start);
		}

		void OptimizedWasmWriter::writeSymbolMap() {
			std::ofstream file(symbolMap);
			auto write
				= [&](Function* func) { file << getFunctionIndex(func->name) << ":" << func->name.str << std::endl; };
			ModuleUtils::iterImportedFunctions(*wasm, write);
			ModuleUtils::iterDefinedFunctions(*wasm, write);
			file.close();
		}

		void OptimizedWasmWriter::initializeDebugInfo() {
			lastDebugLocation = { 0, /* lineNumber = */ 1, 0 };
		}

		void OptimizedWasmWriter::writeSourceMapProlog() {
			*sourceMap << "{\"version\":3,\"sources\":[";
			for(size_t i = 0; i < wasm->debugInfoFileNames.size(); i++) {
				if(i > 0) {
					*sourceMap << ",";
				}
				// TODO respect JSON string encoding, e.g. quotes and control chars.
				*sourceMap << "\"" << wasm->debugInfoFileNames[i] << "\"";
			}
			*sourceMap << "],\"names\":[],\"mappings\":\"";
		}

		void writeBase64VLQ(std::ostream& out, int32_t n) {
			uint32_t value = n >= 0 ? n << 1 : ((-n) << 1) | 1;
			while(1) {
				uint32_t digit = value & 0x1F;
				value >>= 5;
				if(!value) {
					// last VLQ digit -- base64 codes 'A'..'Z', 'a'..'f'
					out << char(digit < 26 ? 'A' + digit : 'a' + digit - 26);
					break;
				}
				// more VLG digit will follow -- add continuation bit (0x20),
				// base64 codes 'g'..'z', '0'..'9', '+', '/'
				out << char(digit < 20 ? 'g' + digit : digit < 30 ? '0' + digit - 20 : digit == 30 ? '+' : '/');
			}
		}

		void OptimizedWasmWriter::writeSourceMapEpilog() {
			// write source map entries
			size_t lastOffset               = 0;
			Function::DebugLocation lastLoc = { 0, /* lineNumber = */ 1, 0 };
			for(const auto& [offset, loc] : sourceMapLocations) {
				if(lastOffset > 0) {
					*sourceMap << ",";
				}
				writeBase64VLQ(*sourceMap, int32_t(offset - lastOffset));
				writeBase64VLQ(*sourceMap, int32_t(loc->fileIndex - lastLoc.fileIndex));
				writeBase64VLQ(*sourceMap, int32_t(loc->lineNumber - lastLoc.lineNumber));
				writeBase64VLQ(*sourceMap, int32_t(loc->columnNumber - lastLoc.columnNumber));
				lastLoc    = *loc;
				lastOffset = offset;
			}
			*sourceMap << "\"}";
		}

		void OptimizedWasmWriter::writeLateUserSections() {
			for(auto& section : wasm->userSections) {
				if(section.name != BinaryConsts::UserSections::Dylink) {
					writeUserSection(section);
				}
			}
		}

		void OptimizedWasmWriter::writeUserSection(const UserSection& section) {
			auto start = startSection(BinaryConsts::User);
			binaryIO.writeInlineString(section.name.c_str());
			for(size_t i = 0; i < section.data.size(); i++) {
				writeValue<ValueWritten::UserSectionData>(section.data[i]);
			}
			finishSection(start);
		}

		void OptimizedWasmWriter::writeFeaturesSection() {
			if(!wasm->hasFeaturesSection || wasm->features.isMVP()) {
				return;
			}

			// TODO(tlively): unify feature names with rest of toolchain and use
			// FeatureSet::toString()
			auto toString = [](FeatureSet::Feature f) {
				switch(f) {
				case FeatureSet::Atomics:
					return BinaryConsts::UserSections::AtomicsFeature;
				case FeatureSet::MutableGlobals:
					return BinaryConsts::UserSections::MutableGlobalsFeature;
				case FeatureSet::TruncSat:
					return BinaryConsts::UserSections::TruncSatFeature;
				case FeatureSet::SIMD:
					return BinaryConsts::UserSections::SIMD128Feature;
				case FeatureSet::BulkMemory:
					return BinaryConsts::UserSections::BulkMemoryFeature;
				case FeatureSet::SignExt:
					return BinaryConsts::UserSections::SignExtFeature;
				case FeatureSet::ExceptionHandling:
					return BinaryConsts::UserSections::ExceptionHandlingFeature;
				case FeatureSet::TailCall:
					return BinaryConsts::UserSections::TailCallFeature;
				case FeatureSet::ReferenceTypes:
					return BinaryConsts::UserSections::ReferenceTypesFeature;
				case FeatureSet::Multivalue:
					return BinaryConsts::UserSections::MultivalueFeature;
				case FeatureSet::GC:
					return BinaryConsts::UserSections::GCFeature;
				case FeatureSet::Memory64:
					return BinaryConsts::UserSections::Memory64Feature;
				case FeatureSet::TypedFunctionReferences:
					return BinaryConsts::UserSections::TypedFunctionReferencesFeature;
				case FeatureSet::RelaxedSIMD:
					return BinaryConsts::UserSections::RelaxedSIMDFeature;
				case FeatureSet::ExtendedConst:
					return BinaryConsts::UserSections::ExtendedConstFeature;
				default:
					WASM_UNREACHABLE("unexpected feature flag");
				}
			};

			std::vector<const char*> features;
			wasm->features.iterFeatures([&](FeatureSet::Feature f) { features.push_back(toString(f)); });

			auto start = startSection(BinaryConsts::User);
			binaryIO.writeInlineString(BinaryConsts::UserSections::TargetFeatures);
			writeValue<ValueWritten::NumFeatures>(features.size());
			for(auto& f : features) {
				writeValue<ValueWritten::FeaturePrefix>(BinaryConsts::FeatureUsed);
				binaryIO.writeInlineString(f);
			}
			finishSection(start);
		}

		void OptimizedWasmWriter::writeLegacyDylinkSection() {
			if(!wasm->dylinkSection) {
				return;
			}

			auto start = startSection(BinaryConsts::User);
			binaryIO.writeInlineString(BinaryConsts::UserSections::Dylink);
			writeValue<ValueWritten::DylinkSection>();
			writeValue<ValueWritten::NumNeededDynlibs>(wasm->dylinkSection->neededDynlibs.size());
			for(auto& neededDynlib : wasm->dylinkSection->neededDynlibs) {
				binaryIO.writeInlineString(neededDynlib.c_str());
			}
			finishSection(start);
		}

		void OptimizedWasmWriter::writeDylinkSection() {
			if(!wasm->dylinkSection) {
				return;
			}

			if(wasm->dylinkSection->isLegacy) {
				writeLegacyDylinkSection();
				return;
			}

			auto start = startSection(BinaryConsts::User);
			binaryIO.writeInlineString(BinaryConsts::UserSections::Dylink0);

			auto substart = startSubsection(BinaryConsts::UserSections::Subsection::DylinkMemInfo);
			writeValue<ValueWritten::DylinkSection>();
			finishSubsection(substart);

			if(wasm->dylinkSection->neededDynlibs.size()) {
				substart = startSubsection(BinaryConsts::UserSections::Subsection::DylinkNeeded);
				writeValue<ValueWritten::NumNeededDynlibs>(wasm->dylinkSection->neededDynlibs.size());
				for(auto& neededDynlib : wasm->dylinkSection->neededDynlibs) {
					binaryIO.writeInlineString(neededDynlib.c_str());
				}
				finishSubsection(substart);
			}

			binaryIO.writeData(wasm->dylinkSection->tail.data(), wasm->dylinkSection->tail.size());
			finishSection(start);
		}

		void OptimizedWasmWriter::writeDebugLocation(const Function::DebugLocation& loc) {
			if(loc == lastDebugLocation) {
				return;
			}
			auto offset = binaryIO.streamOffset();
			sourceMapLocations.emplace_back(offset, &loc);
			lastDebugLocation = loc;
		}

		void OptimizedWasmWriter::writeDebugLocation(Expression* curr, Function* func) {
			if(sourceMap) {
				auto& debugLocations = func->debugLocations;
				auto iter            = debugLocations.find(curr);
				if(iter != debugLocations.end()) {
					writeDebugLocation(iter->second);
				}
			}
			// If this is an instruction in a function, and if the original wasm had
			// binary locations tracked, then track it in the output as well.
			if(func && !func->expressionLocations.empty()) {
				binaryLocations.expressions[curr]
					= BinaryLocations::Span { BinaryLocation(binaryIO.streamOffset()), 0 };
				binaryLocationTrackedExpressionsForFunc.push_back(curr);
			}
		}

		void OptimizedWasmWriter::writeDebugLocationEnd(Expression* curr, Function* func) {
			if(func && !func->expressionLocations.empty()) {
				auto& span = binaryLocations.expressions.at(curr);
				span.end   = binaryIO.streamOffset();
			}
		}

		void OptimizedWasmWriter::writeExtraDebugLocation(Expression* curr, Function* func, size_t id) {
			if(func && !func->expressionLocations.empty()) {
				binaryLocations.delimiters[curr][id] = binaryIO.streamOffset();
			}
		}

		size_t OptimizedWasmWriter::streamAdjustSectionLEB(int32_t start) {
			// section size does not include the reserved bytes of the size field itself
			int32_t size                   = binaryIO.streamOffset() - start - binaryIO.maxSectionSizeBits();
			auto sizeFieldSize             = binaryIO.streamWrite(start, size);
			auto adjustmentForLEBShrinking = binaryIO.maxSectionSizeBits() - sizeFieldSize;
			// We can move things back if the actual LEB for the size doesn't use the
			// maximum 5 bytes. In that case we need to adjust offsets after we move
			// things backwards.
			if(adjustmentForLEBShrinking) {
				// we can save some room, nice
				assert(sizeFieldSize < binaryIO.maxSectionSizeBits());
				binaryIO.streamMove(start + binaryIO.maxSectionSizeBits(), start + binaryIO.maxSectionSizeBits() + size,
					start + sizeFieldSize);
				// binaryIO.streamResize(binaryIO.streamOffset() - adjustmentForLEBShrinking);
				if(sourceMap) {
					for(auto i = sourceMapLocationsSizeAtSectionStart; i < sourceMapLocations.size(); ++i) {
						sourceMapLocations[i].first -= adjustmentForLEBShrinking;
					}
				}
			}
			// The section type byte is right before the LEB for the size; we want
			// offsets that are relative to the body, which is after that section type
			// byte and the the size LEB.
			return start + adjustmentForLEBShrinking + sizeFieldSize;
		}

		void OptimizedWasmWriter::streamAdjustFunctionLEB(
			Function* func, size_t start, size_t size, size_t sizePos, size_t sourceMapSize) {
			auto sizeFieldSize = binaryIO.streamWrite(sizePos, size);
			// We can move things back if the actual LEB for the size doesn't use the
			// maximum 5 bytes. In that case we need to adjust offsets after we move
			// things backwards.
			auto adjustmentForLEBShrinking = binaryIO.maxSectionSizeBits() - sizeFieldSize;
			if(adjustmentForLEBShrinking) {
				// we can save some room, nice
				assert(sizeFieldSize < binaryIO.maxSectionSizeBits());
				binaryIO.streamMove(start, start + size, sizePos + sizeFieldSize);
				// binaryIO.streamResize(binaryIO.streamOffset() - adjustmentForLEBShrinking);
				if(sourceMap) {
					for(auto i = sourceMapSize; i < sourceMapLocations.size(); ++i) {
						sourceMapLocations[i].first -= adjustmentForLEBShrinking;
					}
				}
				for(auto* curr : binaryLocationTrackedExpressionsForFunc) {
					// We added the binary locations, adjust them: they must be relative
					// to the code section.
					auto& span = binaryLocations.expressions[curr];
					span.start -= adjustmentForLEBShrinking;
					span.end -= adjustmentForLEBShrinking;
					auto iter = binaryLocations.delimiters.find(curr);
					if(iter != binaryLocations.delimiters.end()) {
						for(auto& item : iter->second) {
							item -= adjustmentForLEBShrinking;
						}
					}
				}
			}
			if(!binaryLocationTrackedExpressionsForFunc.empty()) {
				binaryLocations.functions[func] = BinaryLocations::FunctionLocations { BinaryLocation(sizePos),
					BinaryLocation(start - adjustmentForLEBShrinking), BinaryLocation(binaryIO.streamOffset()) };
			}
			tableOfContents.functionBodies.emplace_back(func->name, sizePos + sizeFieldSize, size);
			binaryLocationTrackedExpressionsForFunc.clear();
		}

		bool isHexDigit(char ch) {
			return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
		}

		int decodeHexNibble(char ch) {
			return ch <= '9' ? ch & 15 : (ch & 15) + 9;
		}

		void OptimizedWasmWriter::writeEscapedName(const char* name) {
			assert(name);
			if(!strpbrk(name, "\\")) {
				binaryIO.writeInlineString(name);
				return;
			}
			// decode escaped by escapeName (see below) function names
			std::string unescaped;
			int32_t size = strlen(name);
			for(int32_t i = 0; i < size;) {
				char ch = name[i++];
				// support only `\xx` escapes; ignore invalid or unsupported escapes
				if(ch != '\\' || i + 1 >= size || !isHexDigit(name[i]) || !isHexDigit(name[i + 1])) {
					unescaped.push_back(ch);
					continue;
				}
				unescaped.push_back(char((decodeHexNibble(name[i]) << 4) | decodeHexNibble(name[i + 1])));
				i += 2;
			}
			binaryIO.writeInlineString(unescaped.c_str());
		}

		void OptimizedWasmWriter::writeInlineBuffer(const char* data, size_t size) {
			writeValue<ValueWritten::InlineBufferSize>(size);
			binaryIO.writeData(data, size);
		}

		void OptimizedWasmWriter::writeType(Type type) {
			if(type.isRef() && !type.isBasic()) {
				if(type.isNullable()) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::nullable);
				} else {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::nonnullable);
				}
				writeHeapType(type.getHeapType());
				return;
			}
			if(type.isRtt()) {
				auto rtt = type.getRtt();
				if(rtt.hasDepth()) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::rtt_n);
					writeValue<ValueWritten::RTTDepth>(rtt.depth);
				} else {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::rtt);
				}
				writeIndexedHeapType(rtt.heapType);
				return;
			}
			int ret = 0;
			TODO_SINGLE_COMPOUND(type);
			switch(type.getBasic()) {
			// None only used for block signatures. TODO: Separate out?
			case Type::none:
				ret = BinaryConsts::EncodedType::Empty;
				break;
			case Type::i32:
				ret = BinaryConsts::EncodedType::i32;
				break;
			case Type::i64:
				ret = BinaryConsts::EncodedType::i64;
				break;
			case Type::f32:
				ret = BinaryConsts::EncodedType::f32;
				break;
			case Type::f64:
				ret = BinaryConsts::EncodedType::f64;
				break;
			case Type::v128:
				ret = BinaryConsts::EncodedType::v128;
				break;
			case Type::funcref:
				ret = BinaryConsts::EncodedType::funcref;
				break;
			case Type::anyref:
				ret = BinaryConsts::EncodedType::anyref;
				break;
			case Type::eqref:
				ret = BinaryConsts::EncodedType::eqref;
				break;
			case Type::i31ref:
				ret = BinaryConsts::EncodedType::i31ref;
				break;
			case Type::dataref:
				ret = BinaryConsts::EncodedType::dataref;
				break;
			default:
				WASM_UNREACHABLE("unexpected type");
			}
			writeValue<ValueWritten::Type>(ret);
		}

		void OptimizedWasmWriter::writeHeapType(HeapType type) {
			if(type.isSignature() || type.isStruct() || type.isArray()) {
				writeValue<ValueWritten::HeapType>(getTypeIndex(type));
				return;
			}
			int ret = 0;
			if(type.isBasic()) {
				switch(type.getBasic()) {
				case HeapType::func:
					ret = BinaryConsts::EncodedHeapType::func;
					break;
				case HeapType::any:
					ret = BinaryConsts::EncodedHeapType::any;
					break;
				case HeapType::eq:
					ret = BinaryConsts::EncodedHeapType::eq;
					break;
				case HeapType::i31:
					ret = BinaryConsts::EncodedHeapType::i31;
					break;
				case HeapType::data:
					ret = BinaryConsts::EncodedHeapType::data;
					break;
				}
			} else {
				WASM_UNREACHABLE("TODO: compound GC types");
			}
			writeValue<ValueWritten::HeapType>(ret);
		}

		void OptimizedWasmWriter::writeIndexedHeapType(HeapType type) {
			writeValue<ValueWritten::TypeIndex>(getTypeIndex(type));
		}

		void OptimizedWasmWriter::writeField(const Field& field) {
			if(field.type == Type::i32 && field.packedType != Field::not_packed) {
				if(field.packedType == Field::i8) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::i8);
				} else if(field.packedType == Field::i16) {
					writeValue<ValueWritten::Type>(BinaryConsts::EncodedType::i16);
				} else {
					WASM_UNREACHABLE("invalid packed type");
				}
			} else {
				writeType(field.type);
			}
			writeValue<ValueWritten::Mutable>(field.mutable_);
		}

		// reader

		bool OptimizedWasmReader::hasDWARFSections() {
			assert(binaryIO.getPos() == 0);
			getValue<ValueWritten::Magic>(BinaryConsts::Magic);     // magic
			getValue<ValueWritten::Version>(BinaryConsts::Version); // version
			bool has = false;
			while(binaryIO.more()) {
				uint8_t sectionCode = getValue<ValueWritten::SectionStart>();
				uint32_t payloadLen = getValue<ValueWritten::SectionSize>();
				if(uint64_t(binaryIO.getPos()) + uint64_t(payloadLen) > binaryIO.getSize()) {
					throwError("Section extends beyond end of input");
				}
				auto oldPos = binaryIO.getPos();
				if(sectionCode == BinaryConsts::Section::User) {
					auto sectionName = binaryIO.getInlineString();
					if(wasm::Debug::isDWARFSection(sectionName)) {
						has = true;
						break;
					}
				}
				binaryIO.setPos(oldPos + payloadLen);
			}
			binaryIO.setPos(0);
			return has;
		}

		uint64_t OptimizedWasmReader::read() {
			if(DWARF) {
				// In order to update dwarf, we must store info about each IR node's
				// binary position. This has noticeable memory overhead, so we don't do it
				// by default: the user must request it by setting "DWARF", and even if so
				// we scan ahead to see that there actually *are* DWARF sections, so that
				// we don't do unnecessary work.
				if(!hasDWARFSections()) {
					DWARF = false;
				}
			}

			readHeader();
			readSourceMapHeader();

			// read sections until the end
			while(binaryIO.more()) {
				uint8_t sectionCode = getValue<ValueWritten::SectionStart>();
				uint32_t payloadLen = getValue<ValueWritten::SectionSize>();
				if(uint64_t(binaryIO.getPos()) + uint64_t(payloadLen) > binaryIO.getSize()) {
					throwError("Section extends beyond end of input");
				}

				auto oldPos = binaryIO.getPos();

				// note the section in the list of seen sections, as almost no sections can
				// appear more than once, and verify those that shouldn't do not.
				if(sectionCode != BinaryConsts::Section::User && sectionCode != BinaryConsts::Section::Code) {
					if(!seenSections.insert(BinaryConsts::Section(sectionCode)).second) {
						throwError("section seen more than once: " + std::to_string(sectionCode));
					}
				}

				switch(sectionCode) {
				case BinaryConsts::Section::Start:
					readStart();
					break;
				case BinaryConsts::Section::Memory:
					readMemory();
					break;
				case BinaryConsts::Section::Type:
					readTypes();
					break;
				case BinaryConsts::Section::Import:
					readImports();
					break;
				case BinaryConsts::Section::Function:
					readFunctionSignatures();
					break;
				case BinaryConsts::Section::Code:
					if(DWARF) {
						codeSectionLocation = binaryIO.getPos();
					}
					readFunctions();
					break;
				case BinaryConsts::Section::Export:
					readExports();
					break;
				case BinaryConsts::Section::Element:
					readElementSegments();
					break;
				case BinaryConsts::Section::Global:
					readGlobals();
					break;
				case BinaryConsts::Section::Data:
					readDataSegments();
					break;
				case BinaryConsts::Section::DataCount:
					readDataCount();
					break;
				case BinaryConsts::Section::Table:
					readTableDeclarations();
					break;
				case BinaryConsts::Section::Tag:
					readTags();
					break;
				default: {
					readUserSection(payloadLen);
					if(binaryIO.getPos() > oldPos + payloadLen) {
						throwError("bad user section size, started at " + std::to_string(oldPos) + " plus payload "
								   + std::to_string(payloadLen) + " not being equal to new position "
								   + std::to_string(binaryIO.getPos()));
					}
					binaryIO.setPos(oldPos + payloadLen);
				}
				}

				// make sure we advanced exactly past this section
				if(binaryIO.getPos() != oldPos + payloadLen) {
					throwError("bad section size, started at " + std::to_string(oldPos) + " plus payload "
							   + std::to_string(payloadLen) + " not being equal to new position "
							   + std::to_string(binaryIO.getPos()));
				}
			}

			validateBinary();
			processNames();

			return binaryIO.getPos();
		}

		void OptimizedWasmReader::readUserSection(size_t payloadLen) {
			BYN_TRACE("== readUserSection\n");
			auto oldPos      = binaryIO.getPos();
			Name sectionName = binaryIO.getInlineString();
			size_t read      = binaryIO.getPos() - oldPos;
			if(read > payloadLen) {
				throwError("bad user section size");
			}
			payloadLen -= read;
			if(sectionName.equals(BinaryConsts::UserSections::Name)) {
				if(debugInfo) {
					readNames(payloadLen);
				} else {
					binaryIO.setPos(binaryIO.getPos() + payloadLen);
				}
			} else if(sectionName.equals(BinaryConsts::UserSections::TargetFeatures)) {
				readFeatures(payloadLen);
			} else if(sectionName.equals(BinaryConsts::UserSections::Dylink)) {
				readDylink(payloadLen);
			} else if(sectionName.equals(BinaryConsts::UserSections::Dylink0)) {
				readDylink0(payloadLen);
			} else {
				// an unfamiliar custom section
				if(sectionName.equals(BinaryConsts::UserSections::Linking)) {
					std::cerr << "warning: linking section is present, so this is not a standard "
								 "wasm file - binaryen cannot handle this properly!\n";
				}
				wasm.userSections.resize(wasm.userSections.size() + 1);
				auto& section = wasm.userSections.back();
				section.name  = sectionName.str;
				section.data  = binaryIO.getByteView(payloadLen);
			}
		}

		bool OptimizedWasmReader::getBasicType(int32_t code, Type& out) {
			switch(code) {
			case BinaryConsts::EncodedType::i32:
				out = Type::i32;
				return true;
			case BinaryConsts::EncodedType::i64:
				out = Type::i64;
				return true;
			case BinaryConsts::EncodedType::f32:
				out = Type::f32;
				return true;
			case BinaryConsts::EncodedType::f64:
				out = Type::f64;
				return true;
			case BinaryConsts::EncodedType::v128:
				out = Type::v128;
				return true;
			case BinaryConsts::EncodedType::funcref:
				out = Type::funcref;
				return true;
			case BinaryConsts::EncodedType::anyref:
				out = Type::anyref;
				return true;
			case BinaryConsts::EncodedType::eqref:
				out = Type::eqref;
				return true;
			case BinaryConsts::EncodedType::i31ref:
				out = Type(HeapType::i31, NonNullable);
				return true;
			case BinaryConsts::EncodedType::dataref:
				out = Type(HeapType::data, NonNullable);
				return true;
			default:
				return false;
			}
		}

		bool OptimizedWasmReader::getBasicHeapType(int64_t code, HeapType& out) {
			switch(code) {
			case BinaryConsts::EncodedHeapType::func:
				out = HeapType::func;
				return true;
			case BinaryConsts::EncodedHeapType::any:
				out = HeapType::any;
				return true;
			case BinaryConsts::EncodedHeapType::eq:
				out = HeapType::eq;
				return true;
			case BinaryConsts::EncodedHeapType::i31:
				out = HeapType::i31;
				return true;
			case BinaryConsts::EncodedHeapType::data:
				out = HeapType::data;
				return true;
			default:
				return false;
			}
		}

		Type OptimizedWasmReader::getType(int initial) {
			// Single value types are negative; signature indices are non-negative
			if(initial >= 0) {
				// TODO: Handle block input types properly.
				return getSignatureByTypeIndex(initial).results;
			}
			Type type;
			if(getBasicType(initial, type)) {
				return type;
			}
			switch(initial) {
			// None only used for block signatures. TODO: Separate out?
			case BinaryConsts::EncodedType::Empty:
				return Type::none;
			case BinaryConsts::EncodedType::nullable:
				return Type(getHeapType(), Nullable);
			case BinaryConsts::EncodedType::nonnullable:
				return Type(getHeapType(), NonNullable);
			case BinaryConsts::EncodedType::rtt_n: {
				auto depth    = getValue<ValueWritten::RTTDepth>();
				auto heapType = getIndexedHeapType();
				return Type(Rtt(depth, heapType));
			}
			case BinaryConsts::EncodedType::rtt: {
				return Type(Rtt(getIndexedHeapType()));
			}
			default:
				throwError("invalid wasm type: " + std::to_string(initial));
			}
			WASM_UNREACHABLE("unexpected type");
		}

		Type OptimizedWasmReader::getType() {
			return getType(getValue<ValueWritten::Type>());
		}

		HeapType OptimizedWasmReader::getHeapType() {
			auto type = getValue<ValueWritten::HeapType>();
			// Single heap types are negative; heap type indices are non-negative
			if(type >= 0) {
				if(size_t(type) >= types.size()) {
					throwError("invalid signature index: " + std::to_string(type));
				}
				return types[type];
			}
			HeapType ht;
			if(getBasicHeapType(type, ht)) {
				return ht;
			} else {
				throwError("invalid wasm heap type: " + std::to_string(type));
			}
			WASM_UNREACHABLE("unexpected type");
		}

		HeapType OptimizedWasmReader::getIndexedHeapType() {
			auto index = getValue<ValueWritten::TypeIndex>();
			if(index >= types.size()) {
				throwError("invalid heap type index: " + std::to_string(index));
			}
			return types[index];
		}

		Type OptimizedWasmReader::getConcreteType() {
			auto type = getType();
			if(!type.isConcrete()) {
				throw ParseException("non-concrete type when one expected");
			}
			return type;
		}

		void OptimizedWasmReader::readHeader() {
			BYN_TRACE("== readHeader\n");
			getValue<ValueWritten::Magic>(BinaryConsts::Magic);
			getValue<ValueWritten::Version>(BinaryConsts::Version);
		}

		void OptimizedWasmReader::readStart() {
			BYN_TRACE("== readStart\n");
			startIndex = getValue<ValueWritten::FunctionIndex>();
		}

		void OptimizedWasmReader::readMemory() {
			BYN_TRACE("== readMemory\n");
			auto numMemories = getValue<ValueWritten::CountMemories>();
			if(!numMemories) {
				return;
			}
			if(numMemories != 1) {
				throwError("Must be exactly 1 memory");
			}
			if(wasm.memory.exists) {
				throwError("Memory cannot be both imported and defined");
			}
			wasm.memory.exists = true;
			binaryIO.getResizableLimits(wasm.memory.initial, wasm.memory.max, wasm.memory.shared, wasm.memory.indexType,
				Memory::kUnlimitedSize);
		}

		void OptimizedWasmReader::readTypes() {
			BYN_TRACE("== readTypes\n");
			TypeBuilder builder(getValue<ValueWritten::CountTypeGroups>());
			BYN_TRACE("num: " << builder.size() << std::endl);

			auto makeType = [&](int32_t typeCode) {
				Type type;
				if(getBasicType(typeCode, type)) {
					return type;
				}

				switch(typeCode) {
				case BinaryConsts::EncodedType::nullable:
				case BinaryConsts::EncodedType::nonnullable: {
					auto nullability = typeCode == BinaryConsts::EncodedType::nullable ? Nullable : NonNullable;
					int64_t htCode   = getValue<ValueWritten::HeapType>();
					HeapType ht;
					if(getBasicHeapType(htCode, ht)) {
						return Type(ht, nullability);
					}
					if(size_t(htCode) >= builder.size()) {
						throwError("invalid type index: " + std::to_string(htCode));
					}
					return builder.getTempRefType(builder[size_t(htCode)], nullability);
				}
				case BinaryConsts::EncodedType::rtt_n:
				case BinaryConsts::EncodedType::rtt: {
					auto depth  = typeCode == BinaryConsts::EncodedType::rtt ? Rtt::NoDepth
																			 : getValue<ValueWritten::RTTDepth>();
					auto htCode = getValue<ValueWritten::TypeIndex>();
					if(size_t(htCode) >= builder.size()) {
						throwError("invalid type index: " + std::to_string(htCode));
					}
					return builder.getTempRttType(Rtt(depth, builder[htCode]));
				}
				default:
					throwError("unexpected type index: " + std::to_string(typeCode));
				}
				WASM_UNREACHABLE("unexpected type");
			};

			auto readType = [&]() { return makeType(getValue<ValueWritten::Type>()); };

			auto readSignatureDef = [&]() {
				std::vector<Type> params;
				std::vector<Type> results;
				size_t numParams = getValue<ValueWritten::NumFunctionParams>();
				BYN_TRACE("num params: " << numParams << std::endl);
				for(size_t j = 0; j < numParams; j++) {
					params.push_back(readType());
				}
				auto numResults = getValue<ValueWritten::NumFunctionParams>();
				BYN_TRACE("num results: " << numResults << std::endl);
				for(size_t j = 0; j < numResults; j++) {
					results.push_back(readType());
				}
				return Signature(builder.getTempTupleType(params), builder.getTempTupleType(results));
			};

			auto readMutability = [&]() {
				switch(getValue<ValueWritten::Mutable>()) {
				case 0:
					return Immutable;
				case 1:
					return Mutable;
				default:
					throw ParseException("Expected 0 or 1 for mutability");
				}
			};

			auto readFieldDef = [&]() {
				// The value may be a general wasm type, or one of the types only possible
				// in a field.
				auto typeCode = getValue<ValueWritten::Type>();
				if(typeCode == BinaryConsts::EncodedType::i8) {
					auto mutable_ = readMutability();
					return Field(Field::i8, mutable_);
				}
				if(typeCode == BinaryConsts::EncodedType::i16) {
					auto mutable_ = readMutability();
					return Field(Field::i16, mutable_);
				}
				// It's a regular wasm value.
				auto type     = makeType(typeCode);
				auto mutable_ = readMutability();
				return Field(type, mutable_);
			};

			auto readStructDef = [&]() {
				FieldList fields;
				size_t numFields = getValue<ValueWritten::NumStructFields>();
				BYN_TRACE("num fields: " << numFields << std::endl);
				for(size_t j = 0; j < numFields; j++) {
					fields.push_back(readFieldDef());
				}
				return Struct(std::move(fields));
			};

			for(size_t i = 0; i < builder.size(); i++) {
				BYN_TRACE("read one\n");
				auto form = getValue<ValueWritten::Type>();
				if(form == BinaryConsts::EncodedType::Rec) {
					uint32_t groupSize = getValue<ValueWritten::NumTypes>();
					if(getTypeSystem() == TypeSystem::Equirecursive) {
						throwError("Recursion groups not allowed with equirecursive typing");
					}
					if(groupSize == 0u) {
						// TODO: Support groups of size zero by shrinking the builder.
						throwError("Recursion groups of size zero not supported");
					}
					// The group counts as one element in the type section, so we have to
					// allocate space for the extra types.
					builder.grow(groupSize - 1);
					builder.createRecGroup(i, groupSize);
					form = getValue<ValueWritten::Type>();
				}
				std::optional<uint32_t> superIndex;
				if(form == BinaryConsts::EncodedType::Sub) {
					// uint32_t supers = getValue<ValueWritten::NumTypes>();
					// if(supers > 0) {
					//	if(supers != 1) {
					//		throwError("Invalid type definition with " + std::to_string(supers) + " supertypes");
					//	}
					//	superIndex = getValue<ValueWritten::IndexedHeapType>();
					// }
					form = getValue<ValueWritten::Type>();
				}
				if(form == BinaryConsts::EncodedType::Func || form == BinaryConsts::EncodedType::FuncSubtype) {
					builder[i] = readSignatureDef();
				} else if(form == BinaryConsts::EncodedType::Struct
						  || form == BinaryConsts::EncodedType::StructSubtype) {
					builder[i] = readStructDef();
				} else if(form == BinaryConsts::EncodedType::Array || form == BinaryConsts::EncodedType::ArraySubtype) {
					builder[i] = Array(readFieldDef());
				} else {
					throwError("Bad type form " + std::to_string(form));
				}
				if(form == BinaryConsts::EncodedType::FuncSubtype || form == BinaryConsts::EncodedType::StructSubtype
					|| form == BinaryConsts::EncodedType::ArraySubtype) {
					int64_t super = getValue<ValueWritten::HeapType>();
					if(super >= 0) {
						superIndex = (uint32_t)super;
					} else {
						// Validate but otherwise ignore trivial supertypes.
						HeapType basicSuper;
						if(!getBasicHeapType(super, basicSuper)) {
							throwError("Unrecognized supertype " + std::to_string(super));
						}
						if(form == BinaryConsts::EncodedType::FuncSubtype) {
							if(basicSuper != HeapType::func) {
								throwError("The only allowed trivial supertype for functions is func");
							}
						} else {
							if(basicSuper != HeapType::data) {
								throwError("The only allowed trivial supertype for structs and "
										   "arrays is data");
							}
						}
					}
				}
				if(superIndex) {
					if(*superIndex > builder.size()) {
						throwError("Out of bounds supertype index " + std::to_string(*superIndex));
					}
					builder[i].subTypeOf(builder[*superIndex]);
				}
			}

			auto result = builder.build();
			if(auto* err = result.getError()) {
				Fatal() << "Invalid type: " << err->reason << " at index " << err->index;
			}
			types = *result;
		}

		Name OptimizedWasmReader::getFunctionName(Index index) {
			if(index >= wasm.functions.size()) {
				throwError("invalid function index");
			}
			return wasm.functions[index]->name;
		}

		Name OptimizedWasmReader::getTableName(Index index) {
			if(index >= wasm.tables.size()) {
				throwError("invalid table index");
			}
			return wasm.tables[index]->name;
		}

		Name OptimizedWasmReader::getGlobalName(Index index) {
			if(index >= wasm.globals.size()) {
				throwError("invalid global index");
			}
			return wasm.globals[index]->name;
		}

		Name OptimizedWasmReader::getTagName(Index index) {
			if(index >= wasm.tags.size()) {
				throwError("invalid tag index");
			}
			return wasm.tags[index]->name;
		}

		void OptimizedWasmReader::readImports() {
			BYN_TRACE("== readImports\n");
			size_t num = getValue<ValueWritten::CountImports>();
			BYN_TRACE("num: " << num << std::endl);
			Builder builder(wasm);
			size_t tableCounter    = 0;
			size_t memoryCounter   = 0;
			size_t functionCounter = 0;
			size_t globalCounter   = 0;
			size_t tagCounter      = 0;
			for(size_t i = 0; i < num; i++) {
				BYN_TRACE("read one\n");
				auto module = binaryIO.getInlineString();
				auto base   = binaryIO.getInlineString();
				auto kind   = getValue<ValueWritten::ExternalKind>();
				// We set a unique prefix for the name based on the kind. This ensures no
				// collisions between them, which can't occur here (due to the index i) but
				// could occur later due to the names section.
				switch(kind) {
				case ExternalKind::Function: {
					Name name(std::string("fimport$") + std::to_string(functionCounter++));
					auto index = getValue<ValueWritten::TypeIndex>();
					functionTypes.push_back(getTypeByIndex(index));
					auto type = getTypeByIndex(index);
					if(!type.isSignature()) {
						throwError(std::string("Imported function ") + module.str + '.' + base.str
								   + "'s type must be a signature. Given: " + type.toString());
					}
					auto curr    = builder.makeFunction(name, type, {});
					curr->module = module;
					curr->base   = base;
					functionImports.push_back(curr.get());
					wasm.addFunction(std::move(curr));
					break;
				}
				case ExternalKind::Table: {
					Name name(std::string("timport$") + std::to_string(tableCounter++));
					auto table    = builder.makeTable(name);
					table->module = module;
					table->base   = base;
					table->type   = getType();

					bool is_shared;
					Type indexType;
					binaryIO.getResizableLimits(
						table->initial, table->max, is_shared, indexType, Table::kUnlimitedSize);
					if(is_shared) {
						throwError("Tables may not be shared");
					}
					if(indexType == Type::i64) {
						throwError("Tables may not be 64-bit");
					}

					tableImports.push_back(table.get());
					wasm.addTable(std::move(table));
					break;
				}
				case ExternalKind::Memory: {
					Name name(std::string("mimport$") + std::to_string(memoryCounter++));
					wasm.memory.module = module;
					wasm.memory.base   = base;
					wasm.memory.name   = name;
					wasm.memory.exists = true;
					binaryIO.getResizableLimits(wasm.memory.initial, wasm.memory.max, wasm.memory.shared,
						wasm.memory.indexType, Memory::kUnlimitedSize);
					break;
				}
				case ExternalKind::Global: {
					Name name(std::string("gimport$") + std::to_string(globalCounter++));
					auto type     = getConcreteType();
					auto mutable_ = getValue<ValueWritten::Mutable>();
					auto curr
						= builder.makeGlobal(name, type, nullptr, mutable_ ? Builder::Mutable : Builder::Immutable);
					curr->module = module;
					curr->base   = base;
					globalImports.push_back(curr.get());
					wasm.addGlobal(std::move(curr));
					break;
				}
				case ExternalKind::Tag: {
					Name name(std::string("eimport$") + std::to_string(tagCounter++));
					getValue<ValueWritten::Attribute>(); // Reserved 'attribute' field
					auto index   = getValue<ValueWritten::TypeIndex>();
					auto curr    = builder.makeTag(name, getSignatureByTypeIndex(index));
					curr->module = module;
					curr->base   = base;
					wasm.addTag(std::move(curr));
					break;
				}
				default: {
					throwError("bad import kind");
				}
				}
			}
		}

		Name OptimizedWasmReader::getNextLabel() {
			requireFunctionContext("getting a label");
			return Name("label$" + std::to_string(nextLabel++));
		}

		void OptimizedWasmReader::requireFunctionContext(const char* error) {
			if(!currFunction) {
				throwError(std::string("in a non-function context: ") + error);
			}
		}

		void OptimizedWasmReader::readFunctionSignatures() {
			BYN_TRACE("== readFunctionSignatures\n");
			size_t num = getValue<ValueWritten::CountDefinedFunctions>();
			BYN_TRACE("num: " << num << std::endl);
			for(size_t i = 0; i < num; i++) {
				BYN_TRACE("read one\n");
				auto index = getValue<ValueWritten::TypeIndex>();
				functionTypes.push_back(getTypeByIndex(index));
				// Check that the type is a signature.
				getSignatureByTypeIndex(index);
			}
		}

		HeapType OptimizedWasmReader::getTypeByIndex(Index index) {
			if(index >= types.size()) {
				throwError("invalid type index " + std::to_string(index) + " / " + std::to_string(types.size()));
			}
			return types[index];
		}

		HeapType OptimizedWasmReader::getTypeByFunctionIndex(Index index) {
			if(index >= functionTypes.size()) {
				throwError("invalid function index");
			}
			return functionTypes[index];
		}

		Signature OptimizedWasmReader::getSignatureByTypeIndex(Index index) {
			auto heapType = getTypeByIndex(index);
			if(!heapType.isSignature()) {
				throwError("invalid signature type " + heapType.toString());
			}
			return heapType.getSignature();
		}

		Signature OptimizedWasmReader::getSignatureByFunctionIndex(Index index) {
			auto heapType = getTypeByFunctionIndex(index);
			if(!heapType.isSignature()) {
				throwError("invalid signature type " + heapType.toString());
			}
			return heapType.getSignature();
		}

		void OptimizedWasmReader::readFunctions() {
			BYN_TRACE("== readFunctions\n");
			size_t total = getValue<ValueWritten::CountDefinedFunctions>();
			if(total != functionTypes.size() - functionImports.size()) {
				throwError("invalid function section size, must equal types");
			}
			for(size_t i = 0; i < total; i++) {
				BYN_TRACE("read one at " << binaryIO.getPos() << std::endl);
				auto sizePos = binaryIO.getPos();
				size_t size  = getValue<ValueWritten::SectionSize>();
				if(size == 0) {
					throwError("empty function size");
				}
				endOfFunction = binaryIO.getPos() + size;

				auto* func   = new Function;
				func->name   = Name::fromInt(i);
				func->type   = getTypeByFunctionIndex(functionImports.size() + i);
				currFunction = func;

				if(DWARF) {
					func->funcLocation
						= BinaryLocations::FunctionLocations { BinaryLocation(sizePos - codeSectionLocation),
							  BinaryLocation(binaryIO.getPos() - codeSectionLocation),
							  BinaryLocation(binaryIO.getPos() - codeSectionLocation + size) };
				}

				readNextDebugLocation();

				BYN_TRACE("reading " << i << std::endl);

				readVars();

				std::swap(func->prologLocation, debugLocation);
				{
					// process the function body
					BYN_TRACE("processing function: " << i << std::endl);
					nextLabel = 0;
					debugLocation.clear();
					willBeIgnored = false;
					// process body
					assert(breakStack.empty());
					assert(breakTargetNames.empty());
					assert(exceptionTargetNames.empty());
					assert(expressionStack.empty());
					assert(controlFlowStack.empty());
					assert(letStack.empty());
					assert(depth == 0);
					// Even if we are skipping function bodies we need to not skip the start
					// function. That contains important code for wasm-emscripten-finalize in
					// the form of pthread-related segment initializations. As this is just
					// one function, it doesn't add significant time, so the optimization of
					// skipping bodies is still very useful.
					auto currFunctionIndex = functionImports.size() + functions.size();
					bool isStart           = startIndex == currFunctionIndex;
					if(!skipFunctionBodies || isStart) {
						func->body = getBlockOrSingleton(func->getResults());
					} else {
						// When skipping the function body we need to put something valid in
						// their place so we validate. An unreachable is always acceptable
						// there.
						func->body = Builder(wasm).makeUnreachable();

						// Skip reading the contents.
						binaryIO.setPos(endOfFunction);
					}
					assert(depth == 0);
					assert(breakStack.empty());
					assert(breakTargetNames.empty());
					assert(exceptionTargetNames.empty());
					if(!expressionStack.empty()) {
						throwError("stack not empty on function exit");
					}
					assert(controlFlowStack.empty());
					assert(letStack.empty());
					if(binaryIO.getPos() != endOfFunction) {
						throwError("binary offset at function exit not at expected location");
					}
				}

				if(!wasm.features.hasGCNNLocals()) {
					TypeUpdating::handleNonDefaultableLocals(func, wasm);
				}

				std::swap(func->epilogLocation, debugLocation);
				currFunction = nullptr;
				debugLocation.clear();
				functions.push_back(func);
			}
			BYN_TRACE(" end function bodies\n");
		}

		void OptimizedWasmReader::readVars() {
			size_t numLocalTypes = getValue<ValueWritten::NumFunctionLocals>();
			for(size_t t = 0; t < numLocalTypes; t++) {
				auto num  = getValue<ValueWritten::FunctionLocalSize>();
				auto type = getConcreteType();
				while(num > 0) {
					currFunction->vars.push_back(type);
					num--;
				}
			}
		}

		void OptimizedWasmReader::readExports() {
			BYN_TRACE("== readExports\n");
			size_t num = getValue<ValueWritten::CountExports>();
			BYN_TRACE("num: " << num << std::endl);
			std::unordered_set<Name> names;
			for(size_t i = 0; i < num; i++) {
				BYN_TRACE("read one\n");
				auto curr  = new Export;
				curr->name = binaryIO.getInlineString();
				if(!names.emplace(curr->name).second) {
					throwError("duplicate export name");
				}
				curr->kind = getValue<ValueWritten::ExternalKind>();

				uint32_t index;
				switch(curr->kind) {
				case ExternalKind::Function:
					index = getValue<ValueWritten::FunctionIndex>();
					break;
				case ExternalKind::Table:
					index = getValue<ValueWritten::TableIndex>();
					break;
				case ExternalKind::Memory:
					index = getValue<ValueWritten::MemoryIndex>();
					break;
				case ExternalKind::Global:
					index = getValue<ValueWritten::GlobalIndex>();
					break;
				case ExternalKind::Tag:
					index = getValue<ValueWritten::TagIndex>();
					break;
				default:
					WASM_UNREACHABLE("unexpected extern kind");
				}

				exportIndices[curr] = index;
				exportOrder.push_back(curr);
			}
		}

		static int32_t readBase64VLQ(std::istream& in) {
			uint32_t value = 0;
			uint32_t shift = 0;
			while(1) {
				auto ch = in.get();
				if(ch == EOF) {
					throw MapParseException("unexpected EOF in the middle of VLQ");
				}
				if((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch < 'g')) {
					// last number digit
					uint32_t digit = ch < 'a' ? ch - 'A' : ch - 'a' + 26;
					value |= digit << shift;
					break;
				}
				if(!(ch >= 'g' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '+' && ch != '/') {
					throw MapParseException("invalid VLQ digit");
				}
				uint32_t digit = ch > '9' ? ch - 'g' : (ch >= '0' ? ch - '0' + 20 : (ch == '+' ? 30 : 31));
				value |= digit << shift;
				shift += 5;
			}
			return value & 1 ? -int32_t(value >> 1) : int32_t(value >> 1);
		}

		void OptimizedWasmReader::readSourceMapHeader() {
			if(!sourceMap) {
				return;
			}

			auto skipWhitespace = [&]() {
				while(sourceMap->peek() == ' ' || sourceMap->peek() == '\n') {
					sourceMap->get();
				}
			};

			auto maybeReadChar = [&](char expected) {
				if(sourceMap->peek() != expected) {
					return false;
				}
				sourceMap->get();
				return true;
			};

			auto mustReadChar = [&](char expected) {
				char c = sourceMap->get();
				if(c != expected) {
					throw MapParseException(
						std::string("Unexpected char: expected '") + expected + "' got '" + c + "'");
				}
			};

			auto findField = [&](const char* name) {
				bool matching = false;
				size_t len    = strlen(name);
				size_t pos;
				while(1) {
					int ch = sourceMap->get();
					if(ch == EOF) {
						return false;
					}
					if(ch == '\"') {
						if(matching) {
							// we matched a terminating quote.
							if(pos == len) {
								break;
							}
							matching = false;
						} else {
							matching = true;
							pos      = 0;
						}
					} else if(matching && name[pos] == ch) {
						++pos;
					} else if(matching) {
						matching = false;
					}
				}
				skipWhitespace();
				mustReadChar(':');
				skipWhitespace();
				return true;
			};

			auto readString = [&](std::string& str) {
				std::vector<char> vec;
				skipWhitespace();
				mustReadChar('\"');
				if(!maybeReadChar('\"')) {
					while(1) {
						int ch = sourceMap->get();
						if(ch == EOF) {
							throw MapParseException("unexpected EOF in the middle of string");
						}
						if(ch == '\"') {
							break;
						}
						vec.push_back(ch);
					}
				}
				skipWhitespace();
				str = std::string(vec.begin(), vec.end());
			};

			if(!findField("sources")) {
				throw MapParseException("cannot find the 'sources' field in map");
			}

			skipWhitespace();
			mustReadChar('[');
			if(!maybeReadChar(']')) {
				do {
					std::string file;
					readString(file);
					Index index = wasm.debugInfoFileNames.size();
					wasm.debugInfoFileNames.push_back(file);
					debugInfoFileIndices[file] = index;
				} while(maybeReadChar(','));
				mustReadChar(']');
			}

			if(!findField("mappings")) {
				throw MapParseException("cannot find the 'mappings' field in map");
			}

			mustReadChar('\"');
			if(maybeReadChar('\"')) { // empty mappings
				nextDebugLocation.first = 0;
				return;
			}
			// read first debug location
			uint32_t position     = readBase64VLQ(*sourceMap);
			uint32_t fileIndex    = readBase64VLQ(*sourceMap);
			uint32_t lineNumber   = readBase64VLQ(*sourceMap) + 1; // adjust zero-based line number
			uint32_t columnNumber = readBase64VLQ(*sourceMap);
			nextDebugLocation     = { position, { fileIndex, lineNumber, columnNumber } };
		}

		void OptimizedWasmReader::readNextDebugLocation() {
			if(!sourceMap) {
				return;
			}

			while(nextDebugLocation.first && nextDebugLocation.first <= binaryIO.getPos()) {
				debugLocation.clear();
				// use debugLocation only for function expressions
				if(currFunction) {
					debugLocation.insert(nextDebugLocation.second);
				}

				char ch;
				*sourceMap >> ch;
				if(ch == '\"') { // end of records
					nextDebugLocation.first = 0;
					break;
				}
				if(ch != ',') {
					throw MapParseException("Unexpected delimiter");
				}

				int32_t positionDelta     = readBase64VLQ(*sourceMap);
				uint32_t position         = nextDebugLocation.first + positionDelta;
				int32_t fileIndexDelta    = readBase64VLQ(*sourceMap);
				uint32_t fileIndex        = nextDebugLocation.second.fileIndex + fileIndexDelta;
				int32_t lineNumberDelta   = readBase64VLQ(*sourceMap);
				uint32_t lineNumber       = nextDebugLocation.second.lineNumber + lineNumberDelta;
				int32_t columnNumberDelta = readBase64VLQ(*sourceMap);
				uint32_t columnNumber     = nextDebugLocation.second.columnNumber + columnNumberDelta;

				nextDebugLocation = { position, { fileIndex, lineNumber, columnNumber } };
			}
		}

		Expression* OptimizedWasmReader::readExpression() {
			assert(depth == 0);
			processExpressions();
			if(expressionStack.size() != 1) {
				throwError("expected to read a single expression");
			}
			auto* ret = popExpression();
			assert(depth == 0);
			return ret;
		}

		void OptimizedWasmReader::readGlobals() {
			BYN_TRACE("== readGlobals\n");
			size_t num = getValue<ValueWritten::CountGlobals>();
			BYN_TRACE("num: " << num << std::endl);
			for(size_t i = 0; i < num; i++) {
				BYN_TRACE("read one\n");
				auto type     = getConcreteType();
				auto mutable_ = getValue<ValueWritten::Mutable>();
				if(mutable_ & ~1) {
					throwError("Global mutability must be 0 or 1");
				}
				auto* init = readExpression();
				globals.push_back(Builder::makeGlobal(
					"global$" + std::to_string(i), type, init, mutable_ ? Builder::Mutable : Builder::Immutable));
			}
		}

		void OptimizedWasmReader::processExpressions() {
			BYN_TRACE("== processExpressions\n");
			unreachableInTheWasmSense = false;
			while(1) {
				Expression* curr;
				auto ret = readExpression(curr);
				if(!curr) {
					lastSeparator = ret;
					BYN_TRACE("== processExpressions finished\n");
					return;
				}
				pushExpression(curr);
				if(curr->type == Type::unreachable) {
					// Once we see something unreachable, we don't want to add anything else
					// to the stack, as it could be stacky code that is non-representable in
					// our AST. but we do need to skip it.
					// If there is nothing else here, just stop. Otherwise, go into
					// unreachable mode. peek to see what to do.
					if(binaryIO.getPos() == endOfFunction) {
						throwError("Reached function end without seeing End opcode");
					}
					if(!binaryIO.more()) {
						throwError("unexpected end of input");
					}
					auto peek = binaryIO.peekASTNode();
					if(peek == BinaryConsts::End || peek == BinaryConsts::Else || peek == BinaryConsts::Catch
						|| peek == BinaryConsts::CatchAll || peek == BinaryConsts::Delegate) {
						BYN_TRACE("== processExpressions finished with unreachable" << std::endl);
						lastSeparator = BinaryConsts::ASTNodes(peek);
						// Read the byte we peeked at. No new instruction is generated for it.
						Expression* dummy = nullptr;
						readExpression(dummy);
						assert(!dummy);
						return;
					} else {
						skipUnreachableCode();
						return;
					}
				}
			}
		}

		void OptimizedWasmReader::skipUnreachableCode() {
			BYN_TRACE("== skipUnreachableCode\n");
			// preserve the stack, and restore it. it contains the instruction that made
			// us unreachable, and we can ignore anything after it. things after it may
			// pop, we want to undo that
			auto savedStack = expressionStack;
			// note we are entering unreachable code, and note what the state as before so
			// we can restore it
			auto before   = willBeIgnored;
			willBeIgnored = true;
			// clear the stack. nothing should be popped from there anyhow, just stuff
			// can be pushed and then popped. Popping past the top of the stack will
			// result in uneachables being returned
			expressionStack.clear();
			while(1) {
				// set the unreachableInTheWasmSense flag each time, as sub-blocks may set
				// and unset it
				unreachableInTheWasmSense = true;
				Expression* curr;
				auto ret = readExpression(curr);
				if(!curr) {
					BYN_TRACE("== skipUnreachableCode finished\n");
					lastSeparator             = ret;
					unreachableInTheWasmSense = false;
					willBeIgnored             = before;
					expressionStack           = savedStack;
					return;
				}
				pushExpression(curr);
			}
		}

		void OptimizedWasmReader::pushExpression(Expression* curr) {
			auto type = curr->type;
			if(type.isTuple()) {
				// Store tuple to local and push individual extracted values
				Builder builder(wasm);
				// Non-nullable types require special handling as they cannot be stored to
				// a local, so we may need to use a different local type than the original.
				auto localType = type;
				if(!wasm.features.hasGCNNLocals()) {
					std::vector<Type> finalTypes;
					for(auto t : type) {
						if(t.isNonNullable()) {
							t = Type(t.getHeapType(), Nullable);
						}
						finalTypes.push_back(t);
					}
					localType = Type(Tuple(finalTypes));
				}
				requireFunctionContext("pushExpression-tuple");
				Index tuple = builder.addVar(currFunction, localType);
				expressionStack.push_back(builder.makeLocalSet(tuple, curr));
				for(Index i = 0; i < localType.size(); ++i) {
					Expression* value = builder.makeTupleExtract(builder.makeLocalGet(tuple, localType), i);
					if(localType[i] != type[i]) {
						// We modified this to be nullable; undo that.
						value = builder.makeRefAs(RefAsNonNull, value);
					}
					expressionStack.push_back(value);
				}
			} else {
				expressionStack.push_back(curr);
			}
		}

		Expression* OptimizedWasmReader::popExpression() {
			BYN_TRACE("== popExpression\n");
			if(expressionStack.empty()) {
				if(unreachableInTheWasmSense) {
					// in unreachable code, trying to pop past the polymorphic stack
					// area results in receiving unreachables
					BYN_TRACE("== popping unreachable from polymorphic stack" << std::endl);
					return allocator.alloc<Unreachable>();
				}
				throwError("attempted pop from empty stack / beyond block start boundary at "
						   + std::to_string(binaryIO.getPos()));
			}
			// the stack is not empty, and we would not be going out of the current block
			auto ret = expressionStack.back();
			assert(!ret->type.isTuple());
			expressionStack.pop_back();
			return ret;
		}

		Expression* OptimizedWasmReader::popNonVoidExpression() {
			auto* ret = popExpression();
			if(ret->type != Type::none) {
				return ret;
			}
			// we found a void, so this is stacky code that we must handle carefully
			Builder builder(wasm);
			// add elements until we find a non-void
			std::vector<Expression*> expressions;
			expressions.push_back(ret);
			while(1) {
				auto* curr = popExpression();
				expressions.push_back(curr);
				if(curr->type != Type::none) {
					break;
				}
			}
			auto* block = builder.makeBlock();
			while(!expressions.empty()) {
				block->list.push_back(expressions.back());
				expressions.pop_back();
			}
			requireFunctionContext("popping void where we need a new local");
			auto type = block->list[0]->type;
			if(type.isConcrete()) {
				auto local     = builder.addVar(currFunction, type);
				block->list[0] = builder.makeLocalSet(local, block->list[0]);
				block->list.push_back(builder.makeLocalGet(local, type));
			} else {
				assert(type == Type::unreachable);
				// nothing to do here - unreachable anyhow
			}
			block->finalize();
			return block;
		}

		Expression* OptimizedWasmReader::popTuple(size_t numElems) {
			Builder builder(wasm);
			std::vector<Expression*> elements;
			elements.resize(numElems);
			for(size_t i = 0; i < numElems; i++) {
				auto* elem = popNonVoidExpression();
				if(elem->type == Type::unreachable) {
					// All the previously-popped items cannot be reached, so ignore them. We
					// cannot continue popping because there might not be enough items on the
					// expression stack after an unreachable expression. Any remaining
					// elements can stay unperturbed on the stack and will be explicitly
					// dropped by some parent call to pushBlockElements.
					return elem;
				}
				elements[numElems - i - 1] = elem;
			}
			return Builder(wasm).makeTupleMake(std::move(elements));
		}

		Expression* OptimizedWasmReader::popTypedExpression(Type type) {
			if(type.isSingle()) {
				return popNonVoidExpression();
			} else if(type.isTuple()) {
				return popTuple(type.size());
			} else {
				WASM_UNREACHABLE("Invalid popped type");
			}
		}

		void OptimizedWasmReader::validateBinary() {
			if(hasDataCount && wasm.memory.segments.size() != dataCount) {
				throwError("Number of segments does not agree with DataCount section");
			}
		}

		void OptimizedWasmReader::processNames() {
			for(auto* func : functions) {
				wasm.addFunction(func);
			}
			for(auto& global : globals) {
				wasm.addGlobal(std::move(global));
			}
			for(auto& table : tables) {
				wasm.addTable(std::move(table));
			}
			for(auto& segment : elementSegments) {
				wasm.addElementSegment(std::move(segment));
			}

			// now that we have names, apply things

			if(startIndex != static_cast<Index>(-1)) {
				wasm.start = getFunctionName(startIndex);
			}

			for(auto* curr : exportOrder) {
				auto index = exportIndices[curr];
				switch(curr->kind) {
				case ExternalKind::Function: {
					curr->value = getFunctionName(index);
					break;
				}
				case ExternalKind::Table:
					curr->value = getTableName(index);
					break;
				case ExternalKind::Memory:
					curr->value = wasm.memory.name;
					break;
				case ExternalKind::Global:
					curr->value = getGlobalName(index);
					break;
				case ExternalKind::Tag:
					curr->value = getTagName(index);
					break;
				default:
					throwError("bad export kind");
				}
				wasm.addExport(curr);
			}

			for(auto& [index, refs] : functionRefs) {
				for(auto* ref : refs) {
					if(auto* call = ref->dynCast<Call>()) {
						call->target = getFunctionName(index);
					} else if(auto* refFunc = ref->dynCast<RefFunc>()) {
						refFunc->func = getFunctionName(index);
					} else {
						WASM_UNREACHABLE("Invalid type in function references");
					}
				}
			}

			for(auto& [index, refs] : tableRefs) {
				for(auto* ref : refs) {
					if(auto* callIndirect = ref->dynCast<CallIndirect>()) {
						callIndirect->table = getTableName(index);
					} else if(auto* get = ref->dynCast<TableGet>()) {
						get->table = getTableName(index);
					} else if(auto* set = ref->dynCast<TableSet>()) {
						set->table = getTableName(index);
					} else if(auto* size = ref->dynCast<TableSize>()) {
						size->table = getTableName(index);
					} else if(auto* grow = ref->dynCast<TableGrow>()) {
						grow->table = getTableName(index);
					} else {
						WASM_UNREACHABLE("Invalid type in table references");
					}
				}
			}

			for(auto& [index, refs] : globalRefs) {
				for(auto* ref : refs) {
					if(auto* get = ref->dynCast<GlobalGet>()) {
						get->name = getGlobalName(index);
					} else if(auto* set = ref->dynCast<GlobalSet>()) {
						set->name = getGlobalName(index);
					} else {
						WASM_UNREACHABLE("Invalid type in global references");
					}
				}
			}

			// Everything now has its proper name.

			wasm.updateMaps();
		}

		void OptimizedWasmReader::readDataCount() {
			BYN_TRACE("== readDataCount\n");
			hasDataCount = true;
			dataCount    = getValue<ValueWritten::CountMemorySegments>();
		}

		void OptimizedWasmReader::readDataSegments() {
			BYN_TRACE("== readDataSegments\n");
			auto num = getValue<ValueWritten::CountMemorySegments>();
			for(size_t i = 0; i < num; i++) {
				Memory::Segment curr;
				uint32_t flags = getValue<ValueWritten::MemorySegmentFlags>();
				if(flags > 2) {
					throwError("bad segment flags, must be 0, 1, or 2, not " + std::to_string(flags));
				}
				curr.isPassive = flags & BinaryConsts::IsPassive;
				if(flags & BinaryConsts::HasIndex) {
					// Will never be called
					auto memIndex = getValue<ValueWritten::MemoryIndex>();
					if(memIndex != 0) {
						throwError("nonzero memory index");
					}
				}
				if(!curr.isPassive) {
					curr.offset = readExpression();
				}
				auto size = getValue<ValueWritten::InlineBufferSize>();
				curr.data = binaryIO.getByteView(size);
				wasm.memory.segments.push_back(std::move(curr));
			}
		}

		void OptimizedWasmReader::readTableDeclarations() {
			BYN_TRACE("== readTableDeclarations\n");
			auto numTables = getValue<ValueWritten::CountDefinedTables>();

			for(size_t i = 0; i < numTables; i++) {
				auto elemType = getType();
				if(!elemType.isRef()) {
					throwError("Table type must be a reference type");
				}
				auto table = Builder::makeTable(Name::fromInt(i), elemType);
				bool is_shared;
				Type indexType;
				binaryIO.getResizableLimits(table->initial, table->max, is_shared, indexType, Table::kUnlimitedSize);
				if(is_shared) {
					throwError("Tables may not be shared");
				}
				if(indexType == Type::i64) {
					throwError("Tables may not be 64-bit");
				}

				tables.push_back(std::move(table));
			}
		}

		void OptimizedWasmReader::readElementSegments() {
			BYN_TRACE("== readElementSegments\n");
			auto numSegments = getValue<ValueWritten::CountElementSegments>();
			if(numSegments >= Table::kMaxSize) {
				throwError("Too many segments");
			}
			for(size_t i = 0; i < numSegments; i++) {
				auto flags           = getValue<ValueWritten::ElementSegmentFlags>();
				bool isPassive       = (flags & BinaryConsts::IsPassive) != 0;
				bool hasTableIdx     = !isPassive && ((flags & BinaryConsts::HasIndex) != 0);
				bool isDeclarative   = isPassive && ((flags & BinaryConsts::IsDeclarative) != 0);
				bool usesExpressions = (flags & BinaryConsts::UsesExpressions) != 0;

				if(isDeclarative) {
					// Declared segments are needed in wasm text and binary, but not in
					// Binaryen IR; skip over the segment
					auto type = getValue<ValueWritten::ElementSegmentType>();
					WASM_UNUSED(type);
					auto num = getValue<ValueWritten::ElementSegmentSize>();
					for(Index i = 0; i < num; i++) {
						getValue<ValueWritten::FunctionIndex>();
					}
					continue;
				}

				auto segment = std::make_unique<ElementSegment>();
				segment->setName(Name::fromInt(i), false);

				if(!isPassive) {
					Index tableIdx = 0;
					if(hasTableIdx) {
						tableIdx = getValue<ValueWritten::TableIndex>();
					}

					Table* table         = nullptr;
					auto numTableImports = tableImports.size();
					if(tableIdx < numTableImports) {
						table = tableImports[tableIdx];
					} else if(tableIdx - numTableImports < tables.size()) {
						table = tables[tableIdx - numTableImports].get();
					}
					if(!table) {
						throwError("Table index out of range.");
					}

					segment->table  = table->name;
					segment->offset = readExpression();
				}

				if(isPassive || hasTableIdx) {
					if(usesExpressions) {
						segment->type = getType();
						if(!segment->type.isFunction()) {
							throwError("Invalid type for a usesExpressions element segment");
						}
					} else {
						auto elemKind = getValue<ValueWritten::ElementSegmentType>();
						if(elemKind != 0x0) {
							throwError("Invalid kind (!= funcref(0)) since !usesExpressions.");
						}
					}
				}

				auto& segmentData = segment->data;
				auto size         = getValue<ValueWritten::ElementSegmentSize>();
				if(usesExpressions) {
					for(Index j = 0; j < size; j++) {
						segmentData.push_back(readExpression());
					}
				} else {
					for(Index j = 0; j < size; j++) {
						Index index = getValue<ValueWritten::FunctionIndex>();
						auto sig    = getTypeByFunctionIndex(index);
						// Use a placeholder name for now
						auto* refFunc = Builder(wasm).makeRefFunc(Name::fromInt(index), sig);
						functionRefs[index].push_back(refFunc);
						segmentData.push_back(refFunc);
					}
				}

				elementSegments.push_back(std::move(segment));
			}
		}

		void OptimizedWasmReader::readTags() {
			BYN_TRACE("== readTags\n");
			size_t numTags = getValue<ValueWritten::CountDefinedTags>();
			BYN_TRACE("num: " << numTags << std::endl);
			for(size_t i = 0; i < numTags; i++) {
				BYN_TRACE("read one\n");
				getValue<ValueWritten::Attribute>(); // Reserved 'attribute' field
				auto typeIndex = getValue<ValueWritten::TypeIndex>();
				wasm.addTag(Builder::makeTag("tag$" + std::to_string(i), getSignatureByTypeIndex(typeIndex)));
			}
		}

		static bool isIdChar(char ch) {
			return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '!'
				   || ch == '#' || ch == '$' || ch == '%' || ch == '&' || ch == '\'' || ch == '*' || ch == '+'
				   || ch == '-' || ch == '.' || ch == '/' || ch == ':' || ch == '<' || ch == '=' || ch == '>'
				   || ch == '?' || ch == '@' || ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~';
		}

		static char formatNibble(int nibble) {
			return nibble < 10 ? '0' + nibble : 'a' - 10 + nibble;
		}

		Name OptimizedWasmReader::escape(Name name) {
			bool allIdChars = true;
			for(const char* p = name.str; allIdChars && *p; p++) {
				allIdChars = isIdChar(*p);
			}
			if(allIdChars) {
				return name;
			}
			// encode name, if at least one non-idchar (per WebAssembly spec) was found
			std::string escaped;
			for(const char* p = name.str; *p; p++) {
				char ch = *p;
				if(isIdChar(ch)) {
					escaped.push_back(ch);
					continue;
				}
				// replace non-idchar with `\xx` escape
				escaped.push_back('\\');
				escaped.push_back(formatNibble(ch >> 4));
				escaped.push_back(formatNibble(ch & 15));
			}
			return escaped;
		}

		// Performs necessary processing of names from the name section before using
		// them. Specifically it escapes and deduplicates them.
		class NameProcessor {
		public:
			Name process(Name name) {
				return deduplicate(OptimizedWasmReader::escape(name));
			}

		private:
			std::unordered_set<Name> usedNames;

			Name deduplicate(Name base) {
				Name name = base;
				// De-duplicate names by appending .1, .2, etc.
				for(int i = 1; !usedNames.insert(name).second; ++i) {
					name = std::string(base.str) + std::string(".") + std::to_string(i);
				}
				return name;
			}
		};

		void OptimizedWasmReader::readNames(size_t payloadLen) {
			BYN_TRACE("== readNames\n");
			auto sectionPos   = binaryIO.getPos();
			uint32_t lastType = 0;
			while(binaryIO.getPos() < sectionPos + payloadLen) {
				auto nameType = getValue<ValueWritten::SectionStart>();
				if(lastType && nameType <= lastType) {
					std::cerr << "warning: out-of-order name subsection: " << nameType << std::endl;
				}
				lastType            = nameType;
				auto subsectionSize = getValue<ValueWritten::SectionSize>();
				auto subsectionPos  = binaryIO.getPos();
				if(nameType == BinaryConsts::UserSections::Subsection::NameModule) {
					wasm.name = binaryIO.getInlineString();
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameFunction) {
					auto num = getValue<ValueWritten::CountFunctionIndices>();
					NameProcessor processor;
					for(size_t i = 0; i < num; i++) {
						auto index              = getValue<ValueWritten::FunctionIndex>();
						auto rawName            = binaryIO.getInlineString();
						auto name               = processor.process(rawName);
						auto numFunctionImports = functionImports.size();
						if(index < numFunctionImports) {
							functionImports[index]->setExplicitName(name);
						} else if(index - numFunctionImports < functions.size()) {
							functions[index - numFunctionImports]->setExplicitName(name);
						} else {
							std::cerr << "warning: function index out of bounds in name section, "
										 "function subsection: "
									  << std::string(rawName.str) << " at index " << std::to_string(index) << std::endl;
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameLocal) {
					auto numFuncs           = getValue<ValueWritten::CountFunctionIndices>();
					auto numFunctionImports = functionImports.size();
					for(size_t i = 0; i < numFuncs; i++) {
						auto funcIndex = getValue<ValueWritten::FunctionIndex>();
						Function* func = nullptr;
						if(funcIndex < numFunctionImports) {
							func = functionImports[funcIndex];
						} else if(funcIndex - numFunctionImports < functions.size()) {
							func = functions[funcIndex - numFunctionImports];
						} else {
							std::cerr << "warning: function index out of bounds in name section, local "
										 "subsection: "
									  << std::to_string(funcIndex) << std::endl;
						}
						auto numLocals = getValue<ValueWritten::NumFunctionLocals>();
						NameProcessor processor;
						for(size_t j = 0; j < numLocals; j++) {
							auto localIndex   = getValue<ValueWritten::LocalIndex>();
							auto rawLocalName = binaryIO.getInlineString();
							if(!func) {
								continue; // read and discard in case of prior error
							}
							auto localName = processor.process(rawLocalName);
							if(localName.size() == 0) {
								std::cerr << "warning: empty local name at index " << std::to_string(localIndex)
										  << " in function " << std::string(func->name.str) << std::endl;
							} else if(localIndex < func->getNumLocals()) {
								func->localNames[localIndex] = localName;
							} else {
								std::cerr << "warning: local index out of bounds in name "
											 "section, local subsection: "
										  << std::string(rawLocalName.str) << " at index " << std::to_string(localIndex)
										  << " in function " << std::string(func->name.str) << std::endl;
							}
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameType) {
					auto num = getValue<ValueWritten::CountTypeNames>();
					NameProcessor processor;
					for(size_t i = 0; i < num; i++) {
						auto index   = getValue<ValueWritten::TypeIndex>();
						auto rawName = binaryIO.getInlineString();
						auto name    = processor.process(rawName);
						if(index < types.size()) {
							wasm.typeNames[types[index]].name = name;
						} else {
							std::cerr << "warning: type index out of bounds in name section, "
										 "type subsection: "
									  << std::string(rawName.str) << " at index " << std::to_string(index) << std::endl;
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameTable) {
					auto num = getValue<ValueWritten::CountTables>();
					NameProcessor processor;
					for(size_t i = 0; i < num; i++) {
						auto index           = getValue<ValueWritten::TableIndex>();
						auto rawName         = binaryIO.getInlineString();
						auto name            = processor.process(rawName);
						auto numTableImports = tableImports.size();
						auto setTableName    = [&](Table* table) {
                            for(auto& segment : elementSegments) {
                                if(segment->table == table->name) {
                                    segment->table = name;
                                }
                            }
                            table->setExplicitName(name);
						};

						if(index < numTableImports) {
							setTableName(tableImports[index]);
						} else if(index - numTableImports < tables.size()) {
							setTableName(tables[index - numTableImports].get());
						} else {
							std::cerr << "warning: table index out of bounds in name section, "
										 "table subsection: "
									  << std::string(rawName.str) << " at index " << std::to_string(index) << std::endl;
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameElem) {
					auto num = getValue<ValueWritten::CountElementSegments>();
					NameProcessor processor;
					for(size_t i = 0; i < num; i++) {
						auto index   = getValue<ValueWritten::ElementSegmentIndex>();
						auto rawName = binaryIO.getInlineString();
						auto name    = processor.process(rawName);

						if(index < elementSegments.size()) {
							elementSegments[index]->setExplicitName(name);
						} else {
							std::cerr << "warning: elem index out of bounds in name section, "
										 "elem subsection: "
									  << std::string(rawName.str) << " at index " << std::to_string(index) << std::endl;
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameMemory) {
					auto num = getValue<ValueWritten::CountMemories>();
					for(size_t i = 0; i < num; i++) {
						auto index   = getValue<ValueWritten::MemoryIndex>();
						auto rawName = binaryIO.getInlineString();
						if(index == 0) {
							wasm.memory.setExplicitName(escape(rawName));
						} else {
							std::cerr << "warning: memory index out of bounds in name section, "
										 "memory subsection: "
									  << std::string(rawName.str) << " at index " << std::to_string(index) << std::endl;
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameData) {
					auto num = getValue<ValueWritten::CountMemorySegments>();
					for(size_t i = 0; i < num; i++) {
						auto index   = getValue<ValueWritten::MemorySegmentIndex>();
						auto rawName = binaryIO.getInlineString();
						if(index < wasm.memory.segments.size()) {
							wasm.memory.segments[i].name = rawName;
						} else {
							std::cerr << "warning: memory index out of bounds in name section, "
										 "memory subsection: "
									  << std::string(rawName.str) << " at index " << std::to_string(index) << std::endl;
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameGlobal) {
					auto num = getValue<ValueWritten::CountGlobals>();
					NameProcessor processor;
					for(size_t i = 0; i < num; i++) {
						auto index            = getValue<ValueWritten::GlobalIndex>();
						auto rawName          = binaryIO.getInlineString();
						auto name             = processor.process(rawName);
						auto numGlobalImports = globalImports.size();
						if(index < numGlobalImports) {
							globalImports[index]->setExplicitName(name);
						} else if(index - numGlobalImports < globals.size()) {
							globals[index - numGlobalImports]->setExplicitName(name);
						} else {
							std::cerr << "warning: global index out of bounds in name section, "
										 "global subsection: "
									  << std::string(rawName.str) << " at index " << std::to_string(index) << std::endl;
						}
					}
				} else if(nameType == BinaryConsts::UserSections::Subsection::NameField) {
					auto numTypes = getValue<ValueWritten::CountGCFieldTypes>();
					for(size_t i = 0; i < numTypes; i++) {
						auto typeIndex = getValue<ValueWritten::TypeIndex>();
						bool validType = typeIndex < types.size() && types[typeIndex].isStruct();
						if(!validType) {
							std::cerr << "warning: invalid field index in name field section\n";
						}
						auto numFields = getValue<ValueWritten::NumGCFields>();
						NameProcessor processor;
						for(size_t i = 0; i < numFields; i++) {
							auto fieldIndex = getValue<ValueWritten::GCFieldIndex>();
							auto rawName    = binaryIO.getInlineString();
							auto name       = processor.process(rawName);
							if(validType) {
								wasm.typeNames[types[typeIndex]].fieldNames[fieldIndex] = name;
							}
						}
					}
				} else {
					std::cerr << "warning: unknown name subsection with id " << std::to_string(nameType) << " at "
							  << binaryIO.getPos() << std::endl;
					binaryIO.setPos(subsectionPos + subsectionSize);
				}
				if(binaryIO.getPos() != subsectionPos + subsectionSize) {
					throwError("bad names subsection position change");
				}
			}
			if(binaryIO.getPos() != sectionPos + payloadLen) {
				throwError("bad names section position change");
			}
		}

		void OptimizedWasmReader::readFeatures(size_t payloadLen) {
			wasm.hasFeaturesSection = true;

			auto sectionPos    = binaryIO.getPos();
			size_t numFeatures = getValue<ValueWritten::NumFeatures>();
			for(size_t i = 0; i < numFeatures; ++i) {
				uint8_t prefix = getValue<ValueWritten::FeaturePrefix>();

				bool disallowed = prefix == BinaryConsts::FeatureDisallowed;
				bool required   = prefix == BinaryConsts::FeatureRequired;
				bool used       = prefix == BinaryConsts::FeatureUsed;

				if(!disallowed && !required && !used) {
					throwError("Unrecognized feature policy prefix");
				}
				if(required) {
					std::cerr << "warning: required features in feature section are ignored";
				}

				Name name = binaryIO.getInlineString();
				if(binaryIO.getPos() > sectionPos + payloadLen) {
					throwError("ill-formed string extends beyond section");
				}

				FeatureSet feature;
				if(name == BinaryConsts::UserSections::AtomicsFeature) {
					feature = FeatureSet::Atomics;
				} else if(name == BinaryConsts::UserSections::BulkMemoryFeature) {
					feature = FeatureSet::BulkMemory;
				} else if(name == BinaryConsts::UserSections::ExceptionHandlingFeature) {
					feature = FeatureSet::ExceptionHandling;
				} else if(name == BinaryConsts::UserSections::MutableGlobalsFeature) {
					feature = FeatureSet::MutableGlobals;
				} else if(name == BinaryConsts::UserSections::TruncSatFeature) {
					feature = FeatureSet::TruncSat;
				} else if(name == BinaryConsts::UserSections::SignExtFeature) {
					feature = FeatureSet::SignExt;
				} else if(name == BinaryConsts::UserSections::SIMD128Feature) {
					feature = FeatureSet::SIMD;
				} else if(name == BinaryConsts::UserSections::TailCallFeature) {
					feature = FeatureSet::TailCall;
				} else if(name == BinaryConsts::UserSections::ReferenceTypesFeature) {
					feature = FeatureSet::ReferenceTypes;
				} else if(name == BinaryConsts::UserSections::MultivalueFeature) {
					feature = FeatureSet::Multivalue;
				} else if(name == BinaryConsts::UserSections::GCFeature) {
					feature = FeatureSet::GC;
				} else if(name == BinaryConsts::UserSections::Memory64Feature) {
					feature = FeatureSet::Memory64;
				} else if(name == BinaryConsts::UserSections::TypedFunctionReferencesFeature) {
					feature = FeatureSet::TypedFunctionReferences;
				} else if(name == BinaryConsts::UserSections::RelaxedSIMDFeature) {
					feature = FeatureSet::RelaxedSIMD;
				} else if(name == BinaryConsts::UserSections::ExtendedConstFeature) {
					feature = FeatureSet::ExtendedConst;
				} else {
					// Silently ignore unknown features (this may be and old binaryen running
					// on a new wasm).
				}

				if(disallowed && wasm.features.has(feature)) {
					std::cerr << "warning: feature " << feature.toString()
							  << " was enabled by the user, but disallowed in the features section.";
				}
				if(required || used) {
					wasm.features.enable(feature);
				}
			}
			if(binaryIO.getPos() != sectionPos + payloadLen) {
				throwError("bad features section size");
			}
		}

		void OptimizedWasmReader::readDylink(size_t payloadLen) {
			wasm.dylinkSection = make_unique<DylinkSection>();

			auto sectionPos = binaryIO.getPos();

			wasm.dylinkSection->isLegacy = true;
			getValue<ValueWritten::DylinkSection>();

			size_t numNeededDynlibs = getValue<ValueWritten::NumNeededDynlibs>();
			for(size_t i = 0; i < numNeededDynlibs; ++i) {
				wasm.dylinkSection->neededDynlibs.push_back(binaryIO.getInlineString());
			}

			if(binaryIO.getPos() != sectionPos + payloadLen) {
				throwError("bad dylink section size");
			}
		}

		void OptimizedWasmReader::readDylink0(size_t payloadLen) {
			BYN_TRACE("== readDylink0\n");
			auto sectionPos   = binaryIO.getPos();
			uint32_t lastType = 0;

			wasm.dylinkSection = make_unique<DylinkSection>();
			while(binaryIO.getPos() < sectionPos + payloadLen) {
				auto oldPos     = binaryIO.getPos();
				auto dylinkType = getValue<ValueWritten::SectionStart>();
				if(lastType && dylinkType <= lastType) {
					std::cerr << "warning: out-of-order dylink.0 subsection: " << dylinkType << std::endl;
				}
				lastType            = dylinkType;
				auto subsectionSize = getValue<ValueWritten::SectionSize>();
				auto subsectionPos  = binaryIO.getPos();
				if(dylinkType == BinaryConsts::UserSections::Subsection::DylinkMemInfo) {
					getValue<ValueWritten::DylinkSection>();
				} else if(dylinkType == BinaryConsts::UserSections::Subsection::DylinkNeeded) {
					size_t numNeededDynlibs = getValue<ValueWritten::NumNeededDynlibs>();
					for(size_t i = 0; i < numNeededDynlibs; ++i) {
						wasm.dylinkSection->neededDynlibs.push_back(binaryIO.getInlineString());
					}
				} else {
					// Unknown subsection.  Stop parsing now and store the rest of
					// the section verbatim.
					binaryIO.setPos(oldPos);
					size_t remaining         = (sectionPos + payloadLen) - binaryIO.getPos();
					wasm.dylinkSection->tail = binaryIO.getByteView(payloadLen);
					break;
				}
				if(binaryIO.getPos() != subsectionPos + subsectionSize) {
					throwError("bad dylink.0 subsection position change");
				}
			}
		}

		BinaryConsts::ASTNodes OptimizedWasmReader::readExpression(Expression*& curr) {
			if(binaryIO.getPos() == endOfFunction) {
				throwError("Reached function end without seeing End opcode");
			}
			BYN_TRACE("zz recurse into " << ++depth << " at " << binaryIO.getPos() << std::endl);
			readNextDebugLocation();
			std::set<Function::DebugLocation> currDebugLocation;
			if(debugLocation.size()) {
				currDebugLocation.insert(*debugLocation.begin());
			}
			size_t startPos = binaryIO.getPos();
			uint8_t code    = getValue<ValueWritten::ASTNode>();
			BYN_TRACE("readExpression seeing " << (int)code << std::endl);
			switch(code) {
			case BinaryConsts::Block:
				visitBlock((curr = allocator.alloc<Block>())->cast<Block>());
				break;
			case BinaryConsts::If:
				visitIf((curr = allocator.alloc<If>())->cast<If>());
				break;
			case BinaryConsts::Loop:
				visitLoop((curr = allocator.alloc<Loop>())->cast<Loop>());
				break;
			case BinaryConsts::Br:
			case BinaryConsts::BrIf:
				visitBreak((curr = allocator.alloc<Break>())->cast<Break>(), code);
				break; // code distinguishes br from br_if
			case BinaryConsts::BrTable:
				visitSwitch((curr = allocator.alloc<Switch>())->cast<Switch>());
				break;
			case BinaryConsts::CallFunction:
				visitCall((curr = allocator.alloc<Call>())->cast<Call>());
				break;
			case BinaryConsts::CallIndirect:
				visitCallIndirect((curr = allocator.alloc<CallIndirect>())->cast<CallIndirect>());
				break;
			case BinaryConsts::RetCallFunction: {
				auto call      = allocator.alloc<Call>();
				call->isReturn = true;
				curr           = call;
				visitCall(call);
				break;
			}
			case BinaryConsts::RetCallIndirect: {
				auto call      = allocator.alloc<CallIndirect>();
				call->isReturn = true;
				curr           = call;
				visitCallIndirect(call);
				break;
			}
			case BinaryConsts::LocalGet:
				visitLocalGet((curr = allocator.alloc<LocalGet>())->cast<LocalGet>());
				break;
			case BinaryConsts::LocalTee:
			case BinaryConsts::LocalSet:
				visitLocalSet((curr = allocator.alloc<LocalSet>())->cast<LocalSet>(), code);
				break;
			case BinaryConsts::GlobalGet:
				visitGlobalGet((curr = allocator.alloc<GlobalGet>())->cast<GlobalGet>());
				break;
			case BinaryConsts::GlobalSet:
				visitGlobalSet((curr = allocator.alloc<GlobalSet>())->cast<GlobalSet>());
				break;
			case BinaryConsts::Select:
			case BinaryConsts::SelectWithType:
				visitSelect((curr = allocator.alloc<Select>())->cast<Select>(), code);
				break;
			case BinaryConsts::Return:
				visitReturn((curr = allocator.alloc<Return>())->cast<Return>());
				break;
			case BinaryConsts::Nop:
				visitNop((curr = allocator.alloc<Nop>())->cast<Nop>());
				break;
			case BinaryConsts::Unreachable:
				visitUnreachable((curr = allocator.alloc<Unreachable>())->cast<Unreachable>());
				break;
			case BinaryConsts::Drop:
				visitDrop((curr = allocator.alloc<Drop>())->cast<Drop>());
				break;
			case BinaryConsts::End:
				curr = nullptr;
				// Pop the current control flow structure off the stack. If there is none
				// then this is the "end" of the function itself, which also emits an
				// "end" byte.
				if(!controlFlowStack.empty()) {
					controlFlowStack.pop_back();
				}
				break;
			case BinaryConsts::Else:
			case BinaryConsts::Catch:
			case BinaryConsts::CatchAll: {
				curr = nullptr;
				if(DWARF && currFunction) {
					assert(!controlFlowStack.empty());
					auto currControlFlow = controlFlowStack.back();
					BinaryLocation delimiterId;
					if(currControlFlow->is<If>()) {
						delimiterId = BinaryLocations::Else;
					} else {
						// Both Catch and CatchAll can simply append to the list as we go, as
						// we visit them in the right order in the binary, and like the binary
						// we store the CatchAll at the end.
						delimiterId = currFunction->delimiterLocations[currControlFlow].size();
					}
					currFunction->delimiterLocations[currControlFlow][delimiterId] = startPos - codeSectionLocation;
				}
				break;
			}
			case BinaryConsts::Delegate: {
				curr = nullptr;
				if(DWARF && currFunction) {
					assert(!controlFlowStack.empty());
					controlFlowStack.pop_back();
				}
				break;
			}
			case BinaryConsts::RefNull:
				visitRefNull((curr = allocator.alloc<RefNull>())->cast<RefNull>());
				break;
			case BinaryConsts::RefIsNull:
				visitRefIs((curr = allocator.alloc<RefIs>())->cast<RefIs>(), code);
				break;
			case BinaryConsts::RefFunc:
				visitRefFunc((curr = allocator.alloc<RefFunc>())->cast<RefFunc>());
				break;
			case BinaryConsts::RefEq:
				visitRefEq((curr = allocator.alloc<RefEq>())->cast<RefEq>());
				break;
			case BinaryConsts::RefAsNonNull:
				visitRefAs((curr = allocator.alloc<RefAs>())->cast<RefAs>(), code);
				break;
			case BinaryConsts::BrOnNull:
				maybeVisitBrOn(curr, code);
				break;
			case BinaryConsts::BrOnNonNull:
				maybeVisitBrOn(curr, code);
				break;
			case BinaryConsts::TableGet:
				visitTableGet((curr = allocator.alloc<TableGet>())->cast<TableGet>());
				break;
			case BinaryConsts::TableSet:
				visitTableSet((curr = allocator.alloc<TableSet>())->cast<TableSet>());
				break;
			case BinaryConsts::Try:
				visitTryOrTryInBlock(curr);
				break;
			case BinaryConsts::Throw:
				visitThrow((curr = allocator.alloc<Throw>())->cast<Throw>());
				break;
			case BinaryConsts::Rethrow:
				visitRethrow((curr = allocator.alloc<Rethrow>())->cast<Rethrow>());
				break;
			case BinaryConsts::MemorySize: {
				auto size = allocator.alloc<MemorySize>();
				if(wasm.memory.is64()) {
					size->make64();
				}
				curr = size;
				visitMemorySize(size);
				break;
			}
			case BinaryConsts::MemoryGrow: {
				auto grow = allocator.alloc<MemoryGrow>();
				if(wasm.memory.is64()) {
					grow->make64();
				}
				curr = grow;
				visitMemoryGrow(grow);
				break;
			}
			case BinaryConsts::CallRef:
				visitCallRef((curr = allocator.alloc<CallRef>())->cast<CallRef>());
				break;
			case BinaryConsts::RetCallRef: {
				auto call      = allocator.alloc<CallRef>();
				call->isReturn = true;
				curr           = call;
				visitCallRef(call);
				break;
			}
			case BinaryConsts::Let: {
				visitLet((curr = allocator.alloc<Block>())->cast<Block>());
				break;
			}
			case BinaryConsts::AtomicPrefix: {
				code = getValue<ValueWritten::ASTNode>();
				if(maybeVisitLoad(curr, code, /*isAtomic=*/true)) {
					break;
				}
				if(maybeVisitStore(curr, code, /*isAtomic=*/true)) {
					break;
				}
				if(maybeVisitAtomicRMW(curr, code)) {
					break;
				}
				if(maybeVisitAtomicCmpxchg(curr, code)) {
					break;
				}
				if(maybeVisitAtomicWait(curr, code)) {
					break;
				}
				if(maybeVisitAtomicNotify(curr, code)) {
					break;
				}
				if(maybeVisitAtomicFence(curr, code)) {
					break;
				}
				throwError("invalid code after atomic prefix: " + std::to_string(code));
				break;
			}
			case BinaryConsts::MiscPrefix: {
				auto opcode = getValue<ValueWritten::ASTNode32>();
				if(maybeVisitTruncSat(curr, opcode)) {
					break;
				}
				if(maybeVisitMemoryInit(curr, opcode)) {
					break;
				}
				if(maybeVisitDataDrop(curr, opcode)) {
					break;
				}
				if(maybeVisitMemoryCopy(curr, opcode)) {
					break;
				}
				if(maybeVisitMemoryFill(curr, opcode)) {
					break;
				}
				if(maybeVisitTableSize(curr, opcode)) {
					break;
				}
				if(maybeVisitTableGrow(curr, opcode)) {
					break;
				}
				throwError("invalid code after misc prefix: " + std::to_string(opcode));
				break;
			}
			case BinaryConsts::SIMDPrefix: {
				auto opcode = getValue<ValueWritten::ASTNode32>();
				if(maybeVisitSIMDBinary(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDUnary(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDConst(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDStore(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDExtract(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDReplace(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDShuffle(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDTernary(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDShift(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDLoad(curr, opcode)) {
					break;
				}
				if(maybeVisitSIMDLoadStoreLane(curr, opcode)) {
					break;
				}
				throwError("invalid code after SIMD prefix: " + std::to_string(opcode));
				break;
			}
			case BinaryConsts::GCPrefix: {
				auto opcode = getValue<ValueWritten::ASTNode32>();
				if(maybeVisitI31New(curr, opcode)) {
					break;
				}
				if(maybeVisitI31Get(curr, opcode)) {
					break;
				}
				if(maybeVisitRefTest(curr, opcode)) {
					break;
				}
				if(maybeVisitRefCast(curr, opcode)) {
					break;
				}
				if(maybeVisitBrOn(curr, opcode)) {
					break;
				}
				if(maybeVisitRttCanon(curr, opcode)) {
					break;
				}
				if(maybeVisitRttSub(curr, opcode)) {
					break;
				}
				if(maybeVisitStructNew(curr, opcode)) {
					break;
				}
				if(maybeVisitStructGet(curr, opcode)) {
					break;
				}
				if(maybeVisitStructSet(curr, opcode)) {
					break;
				}
				if(maybeVisitArrayNew(curr, opcode)) {
					break;
				}
				if(maybeVisitArrayInit(curr, opcode)) {
					break;
				}
				if(maybeVisitArrayGet(curr, opcode)) {
					break;
				}
				if(maybeVisitArraySet(curr, opcode)) {
					break;
				}
				if(maybeVisitArrayLen(curr, opcode)) {
					break;
				}
				if(maybeVisitArrayCopy(curr, opcode)) {
					break;
				}
				if(opcode == BinaryConsts::RefIsFunc || opcode == BinaryConsts::RefIsData
					|| opcode == BinaryConsts::RefIsI31) {
					visitRefIs((curr = allocator.alloc<RefIs>())->cast<RefIs>(), opcode);
					break;
				}
				if(opcode == BinaryConsts::RefAsFunc || opcode == BinaryConsts::RefAsData
					|| opcode == BinaryConsts::RefAsI31) {
					visitRefAs((curr = allocator.alloc<RefAs>())->cast<RefAs>(), opcode);
					break;
				}
				throwError("invalid code after GC prefix: " + std::to_string(opcode));
				break;
			}
			default: {
				// otherwise, the code is a subcode TODO: optimize
				if(maybeVisitBinary(curr, code)) {
					break;
				}
				if(maybeVisitUnary(curr, code)) {
					break;
				}
				if(maybeVisitConst(curr, code)) {
					break;
				}
				if(maybeVisitLoad(curr, code, /*isAtomic=*/false)) {
					break;
				}
				if(maybeVisitStore(curr, code, /*isAtomic=*/false)) {
					break;
				}
				throwError("bad node code " + std::to_string(code));
				break;
			}
			}
			if(curr) {
				if(currDebugLocation.size()) {
					requireFunctionContext("debugLocation");
					currFunction->debugLocations[curr] = *currDebugLocation.begin();
				}
				if(DWARF && currFunction) {
					currFunction->expressionLocations[curr]
						= BinaryLocations::Span { BinaryLocation(startPos - codeSectionLocation),
							  BinaryLocation(binaryIO.getPos() - codeSectionLocation) };
				}
			}
			BYN_TRACE("zz recurse from " << depth-- << " at " << binaryIO.getPos() << std::endl);
			return BinaryConsts::ASTNodes(code);
		}

		Index OptimizedWasmReader::getAbsoluteLocalIndex(Index index) {
			// Wasm binaries put each let at the bottom of the index space, which may be
			// good for binary size as often the uses of the let variables are close to
			// the let itself. However, in Binaryen IR we just have a simple flat index
			// space of absolute values, which we add to as we parse, and we depend on
			// later optimizations to reorder locals for size.
			//
			// For example, if we have $x, then we add a let with $y, the binary would map
			// 0 => y, 1 => x, while in Binaryen IR $x always stays at 0, and $y is added
			// at 1.
			//
			// Compute the relative index in the let we were added. We start by looking at
			// the last let added, and if we belong to it, we are already relative to it.
			// We will continue relativizing as we go down, til we find our let.
			int64_t relative = index;
			for(auto i = int64_t(letStack.size()) - 1; i >= 0; i--) {
				auto& info      = letStack[i];
				int64_t currNum = info.num;
				// There were |currNum| let items added in this let. Check if we were one of
				// them.
				if(relative < currNum) {
					return info.absoluteStart + relative;
				}
				relative -= currNum;
			}
			// We were not a let, but a normal var from the beginning. In that case, after
			// we subtracted the let items, we have the proper absolute index.
			return relative;
		}

		void OptimizedWasmReader::startControlFlow(Expression* curr) {
			if(DWARF && currFunction) {
				controlFlowStack.push_back(curr);
			}
		}

		void OptimizedWasmReader::pushBlockElements(Block* curr, Type type, size_t start) {
			assert(start <= expressionStack.size());
			// The results of this block are the last values pushed to the expressionStack
			Expression* results = nullptr;
			if(type.isConcrete()) {
				results = popTypedExpression(type);
			}
			if(expressionStack.size() < start) {
				throwError("Block requires more values than are available");
			}
			// Everything else on the stack after `start` is either a none-type expression
			// or a concretely-type expression that is implicitly dropped due to
			// unreachability at the end of the block, like this:
			//
			//  block i32
			//   i32.const 1
			//   i32.const 2
			//   i32.const 3
			//   return
			//  end
			//
			// The first two const elements will be emitted as drops in the block (the
			// optimizer can remove them, of course, but in general we may need dropped
			// items here as they may have side effects).
			//
			for(size_t i = start; i < expressionStack.size(); ++i) {
				auto* item = expressionStack[i];
				if(item->type.isConcrete()) {
					item = Builder(wasm).makeDrop(item);
				}
				curr->list.push_back(item);
			}
			expressionStack.resize(start);
			if(results != nullptr) {
				curr->list.push_back(results);
			}
		}

		void OptimizedWasmReader::visitBlock(Block* curr) {
			BYN_TRACE("zz node: Block\n");
			startControlFlow(curr);
			// special-case Block and de-recurse nested blocks in their first position, as
			// that is a common pattern that can be very highly nested.
			std::vector<Block*> stack;
			while(1) {
				curr->type = getType();
				curr->name = getNextLabel();
				breakStack.push_back({ curr->name, curr->type });
				stack.push_back(curr);
				if(binaryIO.more() && binaryIO.peekASTNode() == BinaryConsts::Block) {
					// a recursion
					readNextDebugLocation();
					curr = allocator.alloc<Block>();
					startControlFlow(curr);
					binaryIO.setPos(binaryIO.getPos() + 1);
					if(debugLocation.size()) {
						requireFunctionContext("block-debugLocation");
						currFunction->debugLocations[curr] = *debugLocation.begin();
					}
					continue;
				} else {
					// end of recursion
					break;
				}
			}
			Block* last = nullptr;
			while(stack.size() > 0) {
				curr = stack.back();
				stack.pop_back();
				// everything after this, that is left when we see the marker, is ours
				size_t start = expressionStack.size();
				if(last) {
					// the previous block is our first-position element
					pushExpression(last);
				}
				last = curr;
				processExpressions();
				size_t end = expressionStack.size();
				if(end < start) {
					throwError("block cannot pop from outside");
				}
				pushBlockElements(curr, curr->type, start);
				curr->finalize(curr->type,
					breakTargetNames.find(curr->name) != breakTargetNames.end() ? Block::HasBreak : Block::NoBreak);
				breakStack.pop_back();
				breakTargetNames.erase(curr->name);
			}
		}

		// Gets a block of expressions. If it's just one, return that singleton.
		Expression* OptimizedWasmReader::getBlockOrSingleton(Type type) {
			Name label = getNextLabel();
			breakStack.push_back({ label, type });
			auto start = expressionStack.size();

			processExpressions();
			size_t end = expressionStack.size();
			if(end < start) {
				throwError("block cannot pop from outside");
			}
			breakStack.pop_back();
			auto* block = allocator.alloc<Block>();
			pushBlockElements(block, type, start);
			block->name = label;
			block->finalize(type);
			// maybe we don't need a block here?
			if(breakTargetNames.find(block->name) == breakTargetNames.end()
				&& exceptionTargetNames.find(block->name) == exceptionTargetNames.end()) {
				block->name = Name();
				if(block->list.size() == 1) {
					return block->list[0];
				}
			}
			breakTargetNames.erase(block->name);
			return block;
		}

		void OptimizedWasmReader::visitIf(If* curr) {
			BYN_TRACE("zz node: If\n");
			startControlFlow(curr);
			curr->type      = getType();
			curr->condition = popNonVoidExpression();
			curr->ifTrue    = getBlockOrSingleton(curr->type);
			if(lastSeparator == BinaryConsts::Else) {
				curr->ifFalse = getBlockOrSingleton(curr->type);
			}
			curr->finalize(curr->type);
			if(lastSeparator != BinaryConsts::End) {
				throwError("if should end with End");
			}
		}

		void OptimizedWasmReader::visitLoop(Loop* curr) {
			BYN_TRACE("zz node: Loop\n");
			startControlFlow(curr);
			curr->type = getType();
			curr->name = getNextLabel();
			breakStack.push_back({ curr->name, Type::none });
			// find the expressions in the block, and create the body
			// a loop may have a list of instructions in wasm, much like
			// a block, but it only has a label at the top of the loop,
			// so even if we need a block (if there is more than 1
			// expression) we never need a label on the block.
			auto start = expressionStack.size();
			processExpressions();
			size_t end = expressionStack.size();
			if(start > end) {
				throwError("block cannot pop from outside");
			}
			if(end - start == 1) {
				curr->body = popExpression();
			} else {
				auto* block = allocator.alloc<Block>();
				pushBlockElements(block, curr->type, start);
				block->finalize(curr->type);
				curr->body = block;
			}
			breakStack.pop_back();
			breakTargetNames.erase(curr->name);
			curr->finalize(curr->type);
		}

		auto OptimizedWasmReader::getBreakTarget(int32_t offset) {
			BYN_TRACE("getBreakTarget " << offset << std::endl);
			if(breakStack.size() < 1 + size_t(offset)) {
				throwError("bad breakindex (low)");
			}
			size_t index = breakStack.size() - 1 - offset;
			if(index >= breakStack.size()) {
				throwError("bad breakindex (high)");
			}
			BYN_TRACE("breaktarget " << breakStack[index].name << " type " << breakStack[index].type << std::endl);
			auto& ret = breakStack[index];
			// if the break is in literally unreachable code, then we will not emit it
			// anyhow, so do not note that the target has breaks to it
			if(!willBeIgnored) {
				breakTargetNames.insert(ret.name);
			}
			return ret;
		}

		Name OptimizedWasmReader::getExceptionTargetName(int32_t offset) {
			BYN_TRACE("getExceptionTarget " << offset << std::endl);
			// We always start parsing a function by creating a block label and pushing it
			// in breakStack in getBlockOrSingleton, so if a 'delegate''s target is that
			// block, it does not mean it targets that block; it throws to the caller.
			if(breakStack.size() - 1 == size_t(offset)) {
				return DELEGATE_CALLER_TARGET;
			}
			size_t index = breakStack.size() - 1 - offset;
			if(index > breakStack.size()) {
				throwError("bad try index (high)");
			}
			BYN_TRACE("exception target " << breakStack[index].name << std::endl);
			auto& ret = breakStack[index];
			// if the delegate/rethrow is in literally unreachable code, then we will not
			// emit it anyhow, so do not note that the target has a reference to it
			if(!willBeIgnored) {
				exceptionTargetNames.insert(ret.name);
			}
			return ret.name;
		}

		void OptimizedWasmReader::visitBreak(Break* curr, uint8_t code) {
			BYN_TRACE("zz node: Break, code " << int32_t(code) << std::endl);
			BreakTarget target = getBreakTarget(getValue<ValueWritten::BreakIndex>());
			curr->name         = target.name;
			if(code == BinaryConsts::BrIf) {
				curr->condition = popNonVoidExpression();
			}
			if(target.type.isConcrete()) {
				curr->value = popTypedExpression(target.type);
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitSwitch(Switch* curr) {
			BYN_TRACE("zz node: Switch\n");
			curr->condition = popNonVoidExpression();
			auto numTargets = getValue<ValueWritten::SwitchTargets>();
			BYN_TRACE("targets: " << numTargets << std::endl);
			for(size_t i = 0; i < numTargets; i++) {
				curr->targets.push_back(getBreakTarget(getValue<ValueWritten::BreakIndex>()).name);
			}
			auto defaultTarget = getBreakTarget(getValue<ValueWritten::BreakIndex>());
			curr->default_     = defaultTarget.name;
			BYN_TRACE("default: " << curr->default_ << "\n");
			if(defaultTarget.type.isConcrete()) {
				curr->value = popTypedExpression(defaultTarget.type);
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitCall(Call* curr) {
			BYN_TRACE("zz node: Call\n");
			auto index = getValue<ValueWritten::FunctionIndex>();
			auto sig   = getSignatureByFunctionIndex(index);
			auto num   = sig.params.size();
			curr->operands.resize(num);
			for(size_t i = 0; i < num; i++) {
				curr->operands[num - i - 1] = popNonVoidExpression();
			}
			curr->type = sig.results;
			functionRefs[index].push_back(curr); // we don't know function names yet
			curr->finalize();
		}

		void OptimizedWasmReader::visitCallIndirect(CallIndirect* curr) {
			BYN_TRACE("zz node: CallIndirect\n");
			auto index     = getValue<ValueWritten::TypeIndex>();
			curr->heapType = getTypeByIndex(index);
			Index tableIdx = getValue<ValueWritten::TableIndex>();
			// TODO: Handle error cases where `heapType` is not a signature?
			auto num = curr->heapType.getSignature().params.size();
			curr->operands.resize(num);
			curr->target = popNonVoidExpression();
			for(size_t i = 0; i < num; i++) {
				curr->operands[num - i - 1] = popNonVoidExpression();
			}
			// Defer setting the table name for later, when we know it.
			tableRefs[tableIdx].push_back(curr);
			curr->finalize();
		}

		void OptimizedWasmReader::visitLocalGet(LocalGet* curr) {
			BYN_TRACE("zz node: LocalGet " << binaryIO.getPos() << std::endl);
			requireFunctionContext("local.get");
			curr->index = getAbsoluteLocalIndex(getValue<ValueWritten::LocalIndex>());
			if(curr->index >= currFunction->getNumLocals()) {
				throwError("bad local.get index");
			}
			curr->type = currFunction->getLocalType(curr->index);
			curr->finalize();
		}

		void OptimizedWasmReader::visitLocalSet(LocalSet* curr, uint8_t code) {
			BYN_TRACE("zz node: Set|LocalTee\n");
			requireFunctionContext("local.set outside of function");
			curr->index = getAbsoluteLocalIndex(getValue<ValueWritten::LocalIndex>());
			if(curr->index >= currFunction->getNumLocals()) {
				throwError("bad local.set index");
			}
			curr->value = popNonVoidExpression();
			if(code == BinaryConsts::LocalTee) {
				curr->makeTee(currFunction->getLocalType(curr->index));
			} else {
				curr->makeSet();
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitGlobalGet(GlobalGet* curr) {
			BYN_TRACE("zz node: GlobalGet " << binaryIO.getPos() << std::endl);
			auto index = getValue<ValueWritten::GlobalIndex>();
			if(index < globalImports.size()) {
				auto* import = globalImports[index];
				curr->name   = import->name;
				curr->type   = import->type;
			} else {
				Index adjustedIndex = index - globalImports.size();
				if(adjustedIndex >= globals.size()) {
					throwError("invalid global index");
				}
				auto& glob = globals[adjustedIndex];
				curr->name = glob->name;
				curr->type = glob->type;
			}
			globalRefs[index].push_back(curr); // we don't know the final name yet
		}

		void OptimizedWasmReader::visitGlobalSet(GlobalSet* curr) {
			BYN_TRACE("zz node: GlobalSet\n");
			auto index = getValue<ValueWritten::GlobalIndex>();
			if(index < globalImports.size()) {
				auto* import = globalImports[index];
				curr->name   = import->name;
			} else {
				Index adjustedIndex = index - globalImports.size();
				if(adjustedIndex >= globals.size()) {
					throwError("invalid global index");
				}
				curr->name = globals[adjustedIndex]->name;
			}
			curr->value = popNonVoidExpression();
			globalRefs[index].push_back(curr); // we don't know the final name yet
			curr->finalize();
		}

		void OptimizedWasmReader::readMemoryAccess(Address& alignment, Address& offset) {
			auto rawAlignment = getValue<ValueWritten::MemoryAccessAlignment>();
			if(rawAlignment > 4) {
				throwError("Alignment must be of a reasonable size");
			}
			alignment = Bits::pow2(rawAlignment);
			offset    = getValue<ValueWritten::MemoryAccessOffset>();
		}

		bool OptimizedWasmReader::maybeVisitLoad(Expression*& out, uint8_t code, bool isAtomic) {
			Load* curr;
			auto allocate = [&]() { curr = allocator.alloc<Load>(); };
			if(!isAtomic) {
				switch(code) {
				case BinaryConsts::I32LoadMem8S:
					allocate();
					curr->bytes   = 1;
					curr->type    = Type::i32;
					curr->signed_ = true;
					break;
				case BinaryConsts::I32LoadMem8U:
					allocate();
					curr->bytes = 1;
					curr->type  = Type::i32;
					break;
				case BinaryConsts::I32LoadMem16S:
					allocate();
					curr->bytes   = 2;
					curr->type    = Type::i32;
					curr->signed_ = true;
					break;
				case BinaryConsts::I32LoadMem16U:
					allocate();
					curr->bytes = 2;
					curr->type  = Type::i32;
					break;
				case BinaryConsts::I32LoadMem:
					allocate();
					curr->bytes = 4;
					curr->type  = Type::i32;
					break;
				case BinaryConsts::I64LoadMem8S:
					allocate();
					curr->bytes   = 1;
					curr->type    = Type::i64;
					curr->signed_ = true;
					break;
				case BinaryConsts::I64LoadMem8U:
					allocate();
					curr->bytes = 1;
					curr->type  = Type::i64;
					break;
				case BinaryConsts::I64LoadMem16S:
					allocate();
					curr->bytes   = 2;
					curr->type    = Type::i64;
					curr->signed_ = true;
					break;
				case BinaryConsts::I64LoadMem16U:
					allocate();
					curr->bytes = 2;
					curr->type  = Type::i64;
					break;
				case BinaryConsts::I64LoadMem32S:
					allocate();
					curr->bytes   = 4;
					curr->type    = Type::i64;
					curr->signed_ = true;
					break;
				case BinaryConsts::I64LoadMem32U:
					allocate();
					curr->bytes = 4;
					curr->type  = Type::i64;
					break;
				case BinaryConsts::I64LoadMem:
					allocate();
					curr->bytes = 8;
					curr->type  = Type::i64;
					break;
				case BinaryConsts::F32LoadMem:
					allocate();
					curr->bytes = 4;
					curr->type  = Type::f32;
					break;
				case BinaryConsts::F64LoadMem:
					allocate();
					curr->bytes = 8;
					curr->type  = Type::f64;
					break;
				default:
					return false;
				}
				BYN_TRACE("zz node: Load\n");
			} else {
				switch(code) {
				case BinaryConsts::I32AtomicLoad8U:
					allocate();
					curr->bytes = 1;
					curr->type  = Type::i32;
					break;
				case BinaryConsts::I32AtomicLoad16U:
					allocate();
					curr->bytes = 2;
					curr->type  = Type::i32;
					break;
				case BinaryConsts::I32AtomicLoad:
					allocate();
					curr->bytes = 4;
					curr->type  = Type::i32;
					break;
				case BinaryConsts::I64AtomicLoad8U:
					allocate();
					curr->bytes = 1;
					curr->type  = Type::i64;
					break;
				case BinaryConsts::I64AtomicLoad16U:
					allocate();
					curr->bytes = 2;
					curr->type  = Type::i64;
					break;
				case BinaryConsts::I64AtomicLoad32U:
					allocate();
					curr->bytes = 4;
					curr->type  = Type::i64;
					break;
				case BinaryConsts::I64AtomicLoad:
					allocate();
					curr->bytes = 8;
					curr->type  = Type::i64;
					break;
				default:
					return false;
				}
				BYN_TRACE("zz node: AtomicLoad\n");
			}

			curr->isAtomic = isAtomic;
			readMemoryAccess(curr->align, curr->offset);
			curr->ptr = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitStore(Expression*& out, uint8_t code, bool isAtomic) {
			Store* curr;
			if(!isAtomic) {
				switch(code) {
				case BinaryConsts::I32StoreMem8:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 1;
					curr->valueType = Type::i32;
					break;
				case BinaryConsts::I32StoreMem16:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 2;
					curr->valueType = Type::i32;
					break;
				case BinaryConsts::I32StoreMem:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 4;
					curr->valueType = Type::i32;
					break;
				case BinaryConsts::I64StoreMem8:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 1;
					curr->valueType = Type::i64;
					break;
				case BinaryConsts::I64StoreMem16:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 2;
					curr->valueType = Type::i64;
					break;
				case BinaryConsts::I64StoreMem32:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 4;
					curr->valueType = Type::i64;
					break;
				case BinaryConsts::I64StoreMem:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 8;
					curr->valueType = Type::i64;
					break;
				case BinaryConsts::F32StoreMem:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 4;
					curr->valueType = Type::f32;
					break;
				case BinaryConsts::F64StoreMem:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 8;
					curr->valueType = Type::f64;
					break;
				default:
					return false;
				}
			} else {
				switch(code) {
				case BinaryConsts::I32AtomicStore8:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 1;
					curr->valueType = Type::i32;
					break;
				case BinaryConsts::I32AtomicStore16:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 2;
					curr->valueType = Type::i32;
					break;
				case BinaryConsts::I32AtomicStore:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 4;
					curr->valueType = Type::i32;
					break;
				case BinaryConsts::I64AtomicStore8:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 1;
					curr->valueType = Type::i64;
					break;
				case BinaryConsts::I64AtomicStore16:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 2;
					curr->valueType = Type::i64;
					break;
				case BinaryConsts::I64AtomicStore32:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 4;
					curr->valueType = Type::i64;
					break;
				case BinaryConsts::I64AtomicStore:
					curr            = allocator.alloc<Store>();
					curr->bytes     = 8;
					curr->valueType = Type::i64;
					break;
				default:
					return false;
				}
			}

			curr->isAtomic = isAtomic;
			BYN_TRACE("zz node: Store\n");
			readMemoryAccess(curr->align, curr->offset);
			curr->value = popNonVoidExpression();
			curr->ptr   = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitAtomicRMW(Expression*& out, uint8_t code) {
			if(code < BinaryConsts::AtomicRMWOps_Begin || code > BinaryConsts::AtomicRMWOps_End) {
				return false;
			}
			auto* curr = allocator.alloc<AtomicRMW>();

			// Set curr to the given opcode, type and size.
#define SET(opcode, optype, size)                                                                                      \
	curr->op    = RMW##opcode;                                                                                         \
	curr->type  = optype;                                                                                              \
	curr->bytes = size

			// Handle the cases for all the valid types for a particular opcode
#define SET_FOR_OP(Op)                                                                                                 \
	case BinaryConsts::I32AtomicRMW##Op:                                                                               \
		SET(Op, Type::i32, 4);                                                                                         \
		break;                                                                                                         \
	case BinaryConsts::I32AtomicRMW##Op##8U:                                                                           \
		SET(Op, Type::i32, 1);                                                                                         \
		break;                                                                                                         \
	case BinaryConsts::I32AtomicRMW##Op##16U:                                                                          \
		SET(Op, Type::i32, 2);                                                                                         \
		break;                                                                                                         \
	case BinaryConsts::I64AtomicRMW##Op:                                                                               \
		SET(Op, Type::i64, 8);                                                                                         \
		break;                                                                                                         \
	case BinaryConsts::I64AtomicRMW##Op##8U:                                                                           \
		SET(Op, Type::i64, 1);                                                                                         \
		break;                                                                                                         \
	case BinaryConsts::I64AtomicRMW##Op##16U:                                                                          \
		SET(Op, Type::i64, 2);                                                                                         \
		break;                                                                                                         \
	case BinaryConsts::I64AtomicRMW##Op##32U:                                                                          \
		SET(Op, Type::i64, 4);                                                                                         \
		break;

			switch(code) {
				SET_FOR_OP(Add);
				SET_FOR_OP(Sub);
				SET_FOR_OP(And);
				SET_FOR_OP(Or);
				SET_FOR_OP(Xor);
				SET_FOR_OP(Xchg);
			default:
				WASM_UNREACHABLE("unexpected opcode");
			}
#undef SET_FOR_OP
#undef SET

			BYN_TRACE("zz node: AtomicRMW\n");
			Address readAlign;
			readMemoryAccess(readAlign, curr->offset);
			if(readAlign != curr->bytes) {
				throwError("Align of AtomicRMW must match size");
			}
			curr->value = popNonVoidExpression();
			curr->ptr   = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitAtomicCmpxchg(Expression*& out, uint8_t code) {
			if(code < BinaryConsts::AtomicCmpxchgOps_Begin || code > BinaryConsts::AtomicCmpxchgOps_End) {
				return false;
			}
			auto* curr = allocator.alloc<AtomicCmpxchg>();

			// Set curr to the given type and size.
#define SET(optype, size)                                                                                              \
	curr->type  = optype;                                                                                              \
	curr->bytes = size

			switch(code) {
			case BinaryConsts::I32AtomicCmpxchg:
				SET(Type::i32, 4);
				break;
			case BinaryConsts::I64AtomicCmpxchg:
				SET(Type::i64, 8);
				break;
			case BinaryConsts::I32AtomicCmpxchg8U:
				SET(Type::i32, 1);
				break;
			case BinaryConsts::I32AtomicCmpxchg16U:
				SET(Type::i32, 2);
				break;
			case BinaryConsts::I64AtomicCmpxchg8U:
				SET(Type::i64, 1);
				break;
			case BinaryConsts::I64AtomicCmpxchg16U:
				SET(Type::i64, 2);
				break;
			case BinaryConsts::I64AtomicCmpxchg32U:
				SET(Type::i64, 4);
				break;
			default:
				WASM_UNREACHABLE("unexpected opcode");
			}

			BYN_TRACE("zz node: AtomicCmpxchg\n");
			Address readAlign;
			readMemoryAccess(readAlign, curr->offset);
			if(readAlign != curr->bytes) {
				throwError("Align of AtomicCpxchg must match size");
			}
			curr->replacement = popNonVoidExpression();
			curr->expected    = popNonVoidExpression();
			curr->ptr         = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitAtomicWait(Expression*& out, uint8_t code) {
			if(code < BinaryConsts::I32AtomicWait || code > BinaryConsts::I64AtomicWait) {
				return false;
			}
			auto* curr = allocator.alloc<AtomicWait>();

			switch(code) {
			case BinaryConsts::I32AtomicWait:
				curr->expectedType = Type::i32;
				break;
			case BinaryConsts::I64AtomicWait:
				curr->expectedType = Type::i64;
				break;
			default:
				WASM_UNREACHABLE("unexpected opcode");
			}
			curr->type = Type::i32;
			BYN_TRACE("zz node: AtomicWait\n");
			curr->timeout  = popNonVoidExpression();
			curr->expected = popNonVoidExpression();
			curr->ptr      = popNonVoidExpression();
			Address readAlign;
			readMemoryAccess(readAlign, curr->offset);
			if(readAlign != curr->expectedType.getByteSize()) {
				throwError("Align of AtomicWait must match size");
			}
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitAtomicNotify(Expression*& out, uint8_t code) {
			if(code != BinaryConsts::AtomicNotify) {
				return false;
			}
			auto* curr = allocator.alloc<AtomicNotify>();
			BYN_TRACE("zz node: AtomicNotify\n");

			curr->type        = Type::i32;
			curr->notifyCount = popNonVoidExpression();
			curr->ptr         = popNonVoidExpression();
			Address readAlign;
			readMemoryAccess(readAlign, curr->offset);
			if(readAlign != curr->type.getByteSize()) {
				throwError("Align of AtomicNotify must match size");
			}
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitAtomicFence(Expression*& out, uint8_t code) {
			if(code != BinaryConsts::AtomicFence) {
				return false;
			}
			auto* curr = allocator.alloc<AtomicFence>();
			BYN_TRACE("zz node: AtomicFence\n");
			curr->order = getValue<ValueWritten::AtomicFenceOrder>();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitConst(Expression*& out, uint8_t code) {
			Const* curr;
			BYN_TRACE("zz node: Const, code " << code << std::endl);
			switch(code) {
			case BinaryConsts::I32Const:
				curr        = allocator.alloc<Const>();
				curr->value = getValue<ValueWritten::ConstS32>();
				break;
			case BinaryConsts::I64Const:
				curr        = allocator.alloc<Const>();
				curr->value = getValue<ValueWritten::ConstS64>();
				break;
			case BinaryConsts::F32Const:
				curr        = allocator.alloc<Const>();
				curr->value = getValue<ValueWritten::ConstF32>();
				break;
			case BinaryConsts::F64Const:
				curr        = allocator.alloc<Const>();
				curr->value = getValue<ValueWritten::ConstF64>();
				break;
			default:
				return false;
			}
			curr->type = curr->value.type;
			out        = curr;

			return true;
		}

		bool OptimizedWasmReader::maybeVisitUnary(Expression*& out, uint8_t code) {
			Unary* curr;
			switch(code) {
			case BinaryConsts::I32Clz:
				curr     = allocator.alloc<Unary>();
				curr->op = ClzInt32;
				break;
			case BinaryConsts::I64Clz:
				curr     = allocator.alloc<Unary>();
				curr->op = ClzInt64;
				break;
			case BinaryConsts::I32Ctz:
				curr     = allocator.alloc<Unary>();
				curr->op = CtzInt32;
				break;
			case BinaryConsts::I64Ctz:
				curr     = allocator.alloc<Unary>();
				curr->op = CtzInt64;
				break;
			case BinaryConsts::I32Popcnt:
				curr     = allocator.alloc<Unary>();
				curr->op = PopcntInt32;
				break;
			case BinaryConsts::I64Popcnt:
				curr     = allocator.alloc<Unary>();
				curr->op = PopcntInt64;
				break;
			case BinaryConsts::I32EqZ:
				curr     = allocator.alloc<Unary>();
				curr->op = EqZInt32;
				break;
			case BinaryConsts::I64EqZ:
				curr     = allocator.alloc<Unary>();
				curr->op = EqZInt64;
				break;
			case BinaryConsts::F32Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegFloat32;
				break;
			case BinaryConsts::F64Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegFloat64;
				break;
			case BinaryConsts::F32Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsFloat32;
				break;
			case BinaryConsts::F64Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsFloat64;
				break;
			case BinaryConsts::F32Ceil:
				curr     = allocator.alloc<Unary>();
				curr->op = CeilFloat32;
				break;
			case BinaryConsts::F64Ceil:
				curr     = allocator.alloc<Unary>();
				curr->op = CeilFloat64;
				break;
			case BinaryConsts::F32Floor:
				curr     = allocator.alloc<Unary>();
				curr->op = FloorFloat32;
				break;
			case BinaryConsts::F64Floor:
				curr     = allocator.alloc<Unary>();
				curr->op = FloorFloat64;
				break;
			case BinaryConsts::F32NearestInt:
				curr     = allocator.alloc<Unary>();
				curr->op = NearestFloat32;
				break;
			case BinaryConsts::F64NearestInt:
				curr     = allocator.alloc<Unary>();
				curr->op = NearestFloat64;
				break;
			case BinaryConsts::F32Sqrt:
				curr     = allocator.alloc<Unary>();
				curr->op = SqrtFloat32;
				break;
			case BinaryConsts::F64Sqrt:
				curr     = allocator.alloc<Unary>();
				curr->op = SqrtFloat64;
				break;
			case BinaryConsts::F32UConvertI32:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertUInt32ToFloat32;
				break;
			case BinaryConsts::F64UConvertI32:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertUInt32ToFloat64;
				break;
			case BinaryConsts::F32SConvertI32:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertSInt32ToFloat32;
				break;
			case BinaryConsts::F64SConvertI32:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertSInt32ToFloat64;
				break;
			case BinaryConsts::F32UConvertI64:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertUInt64ToFloat32;
				break;
			case BinaryConsts::F64UConvertI64:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertUInt64ToFloat64;
				break;
			case BinaryConsts::F32SConvertI64:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertSInt64ToFloat32;
				break;
			case BinaryConsts::F64SConvertI64:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertSInt64ToFloat64;
				break;

			case BinaryConsts::I64SExtendI32:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendSInt32;
				break;
			case BinaryConsts::I64UExtendI32:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendUInt32;
				break;
			case BinaryConsts::I32WrapI64:
				curr     = allocator.alloc<Unary>();
				curr->op = WrapInt64;
				break;

			case BinaryConsts::I32UTruncF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncUFloat32ToInt32;
				break;
			case BinaryConsts::I32UTruncF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncUFloat64ToInt32;
				break;
			case BinaryConsts::I32STruncF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSFloat32ToInt32;
				break;
			case BinaryConsts::I32STruncF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSFloat64ToInt32;
				break;
			case BinaryConsts::I64UTruncF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncUFloat32ToInt64;
				break;
			case BinaryConsts::I64UTruncF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncUFloat64ToInt64;
				break;
			case BinaryConsts::I64STruncF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSFloat32ToInt64;
				break;
			case BinaryConsts::I64STruncF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSFloat64ToInt64;
				break;

			case BinaryConsts::F32Trunc:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncFloat32;
				break;
			case BinaryConsts::F64Trunc:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncFloat64;
				break;

			case BinaryConsts::F32DemoteI64:
				curr     = allocator.alloc<Unary>();
				curr->op = DemoteFloat64;
				break;
			case BinaryConsts::F64PromoteF32:
				curr     = allocator.alloc<Unary>();
				curr->op = PromoteFloat32;
				break;
			case BinaryConsts::I32ReinterpretF32:
				curr     = allocator.alloc<Unary>();
				curr->op = ReinterpretFloat32;
				break;
			case BinaryConsts::I64ReinterpretF64:
				curr     = allocator.alloc<Unary>();
				curr->op = ReinterpretFloat64;
				break;
			case BinaryConsts::F32ReinterpretI32:
				curr     = allocator.alloc<Unary>();
				curr->op = ReinterpretInt32;
				break;
			case BinaryConsts::F64ReinterpretI64:
				curr     = allocator.alloc<Unary>();
				curr->op = ReinterpretInt64;
				break;

			case BinaryConsts::I32ExtendS8:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendS8Int32;
				break;
			case BinaryConsts::I32ExtendS16:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendS16Int32;
				break;
			case BinaryConsts::I64ExtendS8:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendS8Int64;
				break;
			case BinaryConsts::I64ExtendS16:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendS16Int64;
				break;
			case BinaryConsts::I64ExtendS32:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendS32Int64;
				break;

			default:
				return false;
			}
			BYN_TRACE("zz node: Unary\n");
			curr->value = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitTruncSat(Expression*& out, uint32_t code) {
			Unary* curr;
			switch(code) {
			case BinaryConsts::I32STruncSatF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatSFloat32ToInt32;
				break;
			case BinaryConsts::I32UTruncSatF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatUFloat32ToInt32;
				break;
			case BinaryConsts::I32STruncSatF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatSFloat64ToInt32;
				break;
			case BinaryConsts::I32UTruncSatF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatUFloat64ToInt32;
				break;
			case BinaryConsts::I64STruncSatF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatSFloat32ToInt64;
				break;
			case BinaryConsts::I64UTruncSatF32:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatUFloat32ToInt64;
				break;
			case BinaryConsts::I64STruncSatF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatSFloat64ToInt64;
				break;
			case BinaryConsts::I64UTruncSatF64:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatUFloat64ToInt64;
				break;
			default:
				return false;
			}
			BYN_TRACE("zz node: Unary (nontrapping float-to-int)\n");
			curr->value = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitMemoryInit(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::MemoryInit) {
				return false;
			}
			auto* curr    = allocator.alloc<MemoryInit>();
			curr->size    = popNonVoidExpression();
			curr->offset  = popNonVoidExpression();
			curr->dest    = popNonVoidExpression();
			curr->segment = getValue<ValueWritten::MemorySegmentIndex>();
			if(getValue<ValueWritten::MemoryIndex, uint8_t>() != 0) {
				throwError("Unexpected nonzero memory index");
			}
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitDataDrop(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::DataDrop) {
				return false;
			}
			auto* curr    = allocator.alloc<DataDrop>();
			curr->segment = getValue<ValueWritten::MemorySegmentIndex>();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitMemoryCopy(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::MemoryCopy) {
				return false;
			}
			auto* curr   = allocator.alloc<MemoryCopy>();
			curr->size   = popNonVoidExpression();
			curr->source = popNonVoidExpression();
			curr->dest   = popNonVoidExpression();
			if(getValue<ValueWritten::MemoryIndex>() != 0 || getValue<ValueWritten::MemoryIndex>() != 0) {
				throwError("Unexpected nonzero memory index");
			}
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitMemoryFill(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::MemoryFill) {
				return false;
			}
			auto* curr  = allocator.alloc<MemoryFill>();
			curr->size  = popNonVoidExpression();
			curr->value = popNonVoidExpression();
			curr->dest  = popNonVoidExpression();
			if(getValue<ValueWritten::MemoryIndex>() != 0) {
				throwError("Unexpected nonzero memory index");
			}
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitTableSize(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::TableSize) {
				return false;
			}
			Index tableIdx = getValue<ValueWritten::TableIndex>();
			if(tableIdx >= tables.size()) {
				throwError("bad table index");
			}
			auto* curr = allocator.alloc<TableSize>();
			curr->finalize();
			// Defer setting the table name for later, when we know it.
			tableRefs[tableIdx].push_back(curr);
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitTableGrow(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::TableGrow) {
				return false;
			}
			Index tableIdx = getValue<ValueWritten::TableIndex>();
			if(tableIdx >= tables.size()) {
				throwError("bad table index");
			}
			auto* curr  = allocator.alloc<TableGrow>();
			curr->delta = popNonVoidExpression();
			curr->value = popNonVoidExpression();
			curr->finalize();
			// Defer setting the table name for later, when we know it.
			tableRefs[tableIdx].push_back(curr);
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitBinary(Expression*& out, uint8_t code) {
			Binary* curr;
#define INT_TYPED_CODE(code)                                                                                           \
	{                                                                                                                  \
	case BinaryConsts::I32##code:                                                                                      \
		curr     = allocator.alloc<Binary>();                                                                          \
		curr->op = code##Int32;                                                                                        \
		break;                                                                                                         \
	case BinaryConsts::I64##code:                                                                                      \
		curr     = allocator.alloc<Binary>();                                                                          \
		curr->op = code##Int64;                                                                                        \
		break;                                                                                                         \
	}
#define FLOAT_TYPED_CODE(code)                                                                                         \
	{                                                                                                                  \
	case BinaryConsts::F32##code:                                                                                      \
		curr     = allocator.alloc<Binary>();                                                                          \
		curr->op = code##Float32;                                                                                      \
		break;                                                                                                         \
	case BinaryConsts::F64##code:                                                                                      \
		curr     = allocator.alloc<Binary>();                                                                          \
		curr->op = code##Float64;                                                                                      \
		break;                                                                                                         \
	}
#define TYPED_CODE(code)                                                                                               \
	{                                                                                                                  \
		INT_TYPED_CODE(code)                                                                                           \
		FLOAT_TYPED_CODE(code)                                                                                         \
	}

			switch(code) {
				TYPED_CODE(Add);
				TYPED_CODE(Sub);
				TYPED_CODE(Mul);
				INT_TYPED_CODE(DivS);
				INT_TYPED_CODE(DivU);
				INT_TYPED_CODE(RemS);
				INT_TYPED_CODE(RemU);
				INT_TYPED_CODE(And);
				INT_TYPED_CODE(Or);
				INT_TYPED_CODE(Xor);
				INT_TYPED_CODE(Shl);
				INT_TYPED_CODE(ShrU);
				INT_TYPED_CODE(ShrS);
				INT_TYPED_CODE(RotL);
				INT_TYPED_CODE(RotR);
				FLOAT_TYPED_CODE(Div);
				FLOAT_TYPED_CODE(CopySign);
				FLOAT_TYPED_CODE(Min);
				FLOAT_TYPED_CODE(Max);
				TYPED_CODE(Eq);
				TYPED_CODE(Ne);
				INT_TYPED_CODE(LtS);
				INT_TYPED_CODE(LtU);
				INT_TYPED_CODE(LeS);
				INT_TYPED_CODE(LeU);
				INT_TYPED_CODE(GtS);
				INT_TYPED_CODE(GtU);
				INT_TYPED_CODE(GeS);
				INT_TYPED_CODE(GeU);
				FLOAT_TYPED_CODE(Lt);
				FLOAT_TYPED_CODE(Le);
				FLOAT_TYPED_CODE(Gt);
				FLOAT_TYPED_CODE(Ge);
			default:
				return false;
			}
			BYN_TRACE("zz node: Binary\n");
			curr->right = popNonVoidExpression();
			curr->left  = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
#undef TYPED_CODE
#undef INT_TYPED_CODE
#undef FLOAT_TYPED_CODE
		}

		bool OptimizedWasmReader::maybeVisitSIMDBinary(Expression*& out, uint32_t code) {
			Binary* curr;
			switch(code) {
			case BinaryConsts::I8x16Eq:
				curr     = allocator.alloc<Binary>();
				curr->op = EqVecI8x16;
				break;
			case BinaryConsts::I8x16Ne:
				curr     = allocator.alloc<Binary>();
				curr->op = NeVecI8x16;
				break;
			case BinaryConsts::I8x16LtS:
				curr     = allocator.alloc<Binary>();
				curr->op = LtSVecI8x16;
				break;
			case BinaryConsts::I8x16LtU:
				curr     = allocator.alloc<Binary>();
				curr->op = LtUVecI8x16;
				break;
			case BinaryConsts::I8x16GtS:
				curr     = allocator.alloc<Binary>();
				curr->op = GtSVecI8x16;
				break;
			case BinaryConsts::I8x16GtU:
				curr     = allocator.alloc<Binary>();
				curr->op = GtUVecI8x16;
				break;
			case BinaryConsts::I8x16LeS:
				curr     = allocator.alloc<Binary>();
				curr->op = LeSVecI8x16;
				break;
			case BinaryConsts::I8x16LeU:
				curr     = allocator.alloc<Binary>();
				curr->op = LeUVecI8x16;
				break;
			case BinaryConsts::I8x16GeS:
				curr     = allocator.alloc<Binary>();
				curr->op = GeSVecI8x16;
				break;
			case BinaryConsts::I8x16GeU:
				curr     = allocator.alloc<Binary>();
				curr->op = GeUVecI8x16;
				break;
			case BinaryConsts::I16x8Eq:
				curr     = allocator.alloc<Binary>();
				curr->op = EqVecI16x8;
				break;
			case BinaryConsts::I16x8Ne:
				curr     = allocator.alloc<Binary>();
				curr->op = NeVecI16x8;
				break;
			case BinaryConsts::I16x8LtS:
				curr     = allocator.alloc<Binary>();
				curr->op = LtSVecI16x8;
				break;
			case BinaryConsts::I16x8LtU:
				curr     = allocator.alloc<Binary>();
				curr->op = LtUVecI16x8;
				break;
			case BinaryConsts::I16x8GtS:
				curr     = allocator.alloc<Binary>();
				curr->op = GtSVecI16x8;
				break;
			case BinaryConsts::I16x8GtU:
				curr     = allocator.alloc<Binary>();
				curr->op = GtUVecI16x8;
				break;
			case BinaryConsts::I16x8LeS:
				curr     = allocator.alloc<Binary>();
				curr->op = LeSVecI16x8;
				break;
			case BinaryConsts::I16x8LeU:
				curr     = allocator.alloc<Binary>();
				curr->op = LeUVecI16x8;
				break;
			case BinaryConsts::I16x8GeS:
				curr     = allocator.alloc<Binary>();
				curr->op = GeSVecI16x8;
				break;
			case BinaryConsts::I16x8GeU:
				curr     = allocator.alloc<Binary>();
				curr->op = GeUVecI16x8;
				break;
			case BinaryConsts::I32x4Eq:
				curr     = allocator.alloc<Binary>();
				curr->op = EqVecI32x4;
				break;
			case BinaryConsts::I32x4Ne:
				curr     = allocator.alloc<Binary>();
				curr->op = NeVecI32x4;
				break;
			case BinaryConsts::I32x4LtS:
				curr     = allocator.alloc<Binary>();
				curr->op = LtSVecI32x4;
				break;
			case BinaryConsts::I32x4LtU:
				curr     = allocator.alloc<Binary>();
				curr->op = LtUVecI32x4;
				break;
			case BinaryConsts::I32x4GtS:
				curr     = allocator.alloc<Binary>();
				curr->op = GtSVecI32x4;
				break;
			case BinaryConsts::I32x4GtU:
				curr     = allocator.alloc<Binary>();
				curr->op = GtUVecI32x4;
				break;
			case BinaryConsts::I32x4LeS:
				curr     = allocator.alloc<Binary>();
				curr->op = LeSVecI32x4;
				break;
			case BinaryConsts::I32x4LeU:
				curr     = allocator.alloc<Binary>();
				curr->op = LeUVecI32x4;
				break;
			case BinaryConsts::I32x4GeS:
				curr     = allocator.alloc<Binary>();
				curr->op = GeSVecI32x4;
				break;
			case BinaryConsts::I32x4GeU:
				curr     = allocator.alloc<Binary>();
				curr->op = GeUVecI32x4;
				break;
			case BinaryConsts::I64x2Eq:
				curr     = allocator.alloc<Binary>();
				curr->op = EqVecI64x2;
				break;
			case BinaryConsts::I64x2Ne:
				curr     = allocator.alloc<Binary>();
				curr->op = NeVecI64x2;
				break;
			case BinaryConsts::I64x2LtS:
				curr     = allocator.alloc<Binary>();
				curr->op = LtSVecI64x2;
				break;
			case BinaryConsts::I64x2GtS:
				curr     = allocator.alloc<Binary>();
				curr->op = GtSVecI64x2;
				break;
			case BinaryConsts::I64x2LeS:
				curr     = allocator.alloc<Binary>();
				curr->op = LeSVecI64x2;
				break;
			case BinaryConsts::I64x2GeS:
				curr     = allocator.alloc<Binary>();
				curr->op = GeSVecI64x2;
				break;
			case BinaryConsts::F32x4Eq:
				curr     = allocator.alloc<Binary>();
				curr->op = EqVecF32x4;
				break;
			case BinaryConsts::F32x4Ne:
				curr     = allocator.alloc<Binary>();
				curr->op = NeVecF32x4;
				break;
			case BinaryConsts::F32x4Lt:
				curr     = allocator.alloc<Binary>();
				curr->op = LtVecF32x4;
				break;
			case BinaryConsts::F32x4Gt:
				curr     = allocator.alloc<Binary>();
				curr->op = GtVecF32x4;
				break;
			case BinaryConsts::F32x4Le:
				curr     = allocator.alloc<Binary>();
				curr->op = LeVecF32x4;
				break;
			case BinaryConsts::F32x4Ge:
				curr     = allocator.alloc<Binary>();
				curr->op = GeVecF32x4;
				break;
			case BinaryConsts::F64x2Eq:
				curr     = allocator.alloc<Binary>();
				curr->op = EqVecF64x2;
				break;
			case BinaryConsts::F64x2Ne:
				curr     = allocator.alloc<Binary>();
				curr->op = NeVecF64x2;
				break;
			case BinaryConsts::F64x2Lt:
				curr     = allocator.alloc<Binary>();
				curr->op = LtVecF64x2;
				break;
			case BinaryConsts::F64x2Gt:
				curr     = allocator.alloc<Binary>();
				curr->op = GtVecF64x2;
				break;
			case BinaryConsts::F64x2Le:
				curr     = allocator.alloc<Binary>();
				curr->op = LeVecF64x2;
				break;
			case BinaryConsts::F64x2Ge:
				curr     = allocator.alloc<Binary>();
				curr->op = GeVecF64x2;
				break;
			case BinaryConsts::V128And:
				curr     = allocator.alloc<Binary>();
				curr->op = AndVec128;
				break;
			case BinaryConsts::V128Or:
				curr     = allocator.alloc<Binary>();
				curr->op = OrVec128;
				break;
			case BinaryConsts::V128Xor:
				curr     = allocator.alloc<Binary>();
				curr->op = XorVec128;
				break;
			case BinaryConsts::V128Andnot:
				curr     = allocator.alloc<Binary>();
				curr->op = AndNotVec128;
				break;
			case BinaryConsts::I8x16Add:
				curr     = allocator.alloc<Binary>();
				curr->op = AddVecI8x16;
				break;
			case BinaryConsts::I8x16AddSatS:
				curr     = allocator.alloc<Binary>();
				curr->op = AddSatSVecI8x16;
				break;
			case BinaryConsts::I8x16AddSatU:
				curr     = allocator.alloc<Binary>();
				curr->op = AddSatUVecI8x16;
				break;
			case BinaryConsts::I8x16Sub:
				curr     = allocator.alloc<Binary>();
				curr->op = SubVecI8x16;
				break;
			case BinaryConsts::I8x16SubSatS:
				curr     = allocator.alloc<Binary>();
				curr->op = SubSatSVecI8x16;
				break;
			case BinaryConsts::I8x16SubSatU:
				curr     = allocator.alloc<Binary>();
				curr->op = SubSatUVecI8x16;
				break;
			case BinaryConsts::I8x16MinS:
				curr     = allocator.alloc<Binary>();
				curr->op = MinSVecI8x16;
				break;
			case BinaryConsts::I8x16MinU:
				curr     = allocator.alloc<Binary>();
				curr->op = MinUVecI8x16;
				break;
			case BinaryConsts::I8x16MaxS:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxSVecI8x16;
				break;
			case BinaryConsts::I8x16MaxU:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxUVecI8x16;
				break;
			case BinaryConsts::I8x16AvgrU:
				curr     = allocator.alloc<Binary>();
				curr->op = AvgrUVecI8x16;
				break;
			case BinaryConsts::I16x8Add:
				curr     = allocator.alloc<Binary>();
				curr->op = AddVecI16x8;
				break;
			case BinaryConsts::I16x8AddSatS:
				curr     = allocator.alloc<Binary>();
				curr->op = AddSatSVecI16x8;
				break;
			case BinaryConsts::I16x8AddSatU:
				curr     = allocator.alloc<Binary>();
				curr->op = AddSatUVecI16x8;
				break;
			case BinaryConsts::I16x8Sub:
				curr     = allocator.alloc<Binary>();
				curr->op = SubVecI16x8;
				break;
			case BinaryConsts::I16x8SubSatS:
				curr     = allocator.alloc<Binary>();
				curr->op = SubSatSVecI16x8;
				break;
			case BinaryConsts::I16x8SubSatU:
				curr     = allocator.alloc<Binary>();
				curr->op = SubSatUVecI16x8;
				break;
			case BinaryConsts::I16x8Mul:
				curr     = allocator.alloc<Binary>();
				curr->op = MulVecI16x8;
				break;
			case BinaryConsts::I16x8MinS:
				curr     = allocator.alloc<Binary>();
				curr->op = MinSVecI16x8;
				break;
			case BinaryConsts::I16x8MinU:
				curr     = allocator.alloc<Binary>();
				curr->op = MinUVecI16x8;
				break;
			case BinaryConsts::I16x8MaxS:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxSVecI16x8;
				break;
			case BinaryConsts::I16x8MaxU:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxUVecI16x8;
				break;
			case BinaryConsts::I16x8AvgrU:
				curr     = allocator.alloc<Binary>();
				curr->op = AvgrUVecI16x8;
				break;
			case BinaryConsts::I16x8Q15MulrSatS:
				curr     = allocator.alloc<Binary>();
				curr->op = Q15MulrSatSVecI16x8;
				break;
			case BinaryConsts::I16x8ExtmulLowI8x16S:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulLowSVecI16x8;
				break;
			case BinaryConsts::I16x8ExtmulHighI8x16S:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulHighSVecI16x8;
				break;
			case BinaryConsts::I16x8ExtmulLowI8x16U:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulLowUVecI16x8;
				break;
			case BinaryConsts::I16x8ExtmulHighI8x16U:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulHighUVecI16x8;
				break;
			case BinaryConsts::I32x4Add:
				curr     = allocator.alloc<Binary>();
				curr->op = AddVecI32x4;
				break;
			case BinaryConsts::I32x4Sub:
				curr     = allocator.alloc<Binary>();
				curr->op = SubVecI32x4;
				break;
			case BinaryConsts::I32x4Mul:
				curr     = allocator.alloc<Binary>();
				curr->op = MulVecI32x4;
				break;
			case BinaryConsts::I32x4MinS:
				curr     = allocator.alloc<Binary>();
				curr->op = MinSVecI32x4;
				break;
			case BinaryConsts::I32x4MinU:
				curr     = allocator.alloc<Binary>();
				curr->op = MinUVecI32x4;
				break;
			case BinaryConsts::I32x4MaxS:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxSVecI32x4;
				break;
			case BinaryConsts::I32x4MaxU:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxUVecI32x4;
				break;
			case BinaryConsts::I32x4DotI16x8S:
				curr     = allocator.alloc<Binary>();
				curr->op = DotSVecI16x8ToVecI32x4;
				break;
			case BinaryConsts::I32x4ExtmulLowI16x8S:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulLowSVecI32x4;
				break;
			case BinaryConsts::I32x4ExtmulHighI16x8S:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulHighSVecI32x4;
				break;
			case BinaryConsts::I32x4ExtmulLowI16x8U:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulLowUVecI32x4;
				break;
			case BinaryConsts::I32x4ExtmulHighI16x8U:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulHighUVecI32x4;
				break;
			case BinaryConsts::I64x2Add:
				curr     = allocator.alloc<Binary>();
				curr->op = AddVecI64x2;
				break;
			case BinaryConsts::I64x2Sub:
				curr     = allocator.alloc<Binary>();
				curr->op = SubVecI64x2;
				break;
			case BinaryConsts::I64x2Mul:
				curr     = allocator.alloc<Binary>();
				curr->op = MulVecI64x2;
				break;
			case BinaryConsts::I64x2ExtmulLowI32x4S:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulLowSVecI64x2;
				break;
			case BinaryConsts::I64x2ExtmulHighI32x4S:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulHighSVecI64x2;
				break;
			case BinaryConsts::I64x2ExtmulLowI32x4U:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulLowUVecI64x2;
				break;
			case BinaryConsts::I64x2ExtmulHighI32x4U:
				curr     = allocator.alloc<Binary>();
				curr->op = ExtMulHighUVecI64x2;
				break;
			case BinaryConsts::F32x4Add:
				curr     = allocator.alloc<Binary>();
				curr->op = AddVecF32x4;
				break;
			case BinaryConsts::F32x4Sub:
				curr     = allocator.alloc<Binary>();
				curr->op = SubVecF32x4;
				break;
			case BinaryConsts::F32x4Mul:
				curr     = allocator.alloc<Binary>();
				curr->op = MulVecF32x4;
				break;
			case BinaryConsts::F32x4Div:
				curr     = allocator.alloc<Binary>();
				curr->op = DivVecF32x4;
				break;
			case BinaryConsts::F32x4Min:
				curr     = allocator.alloc<Binary>();
				curr->op = MinVecF32x4;
				break;
			case BinaryConsts::F32x4Max:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxVecF32x4;
				break;
			case BinaryConsts::F32x4Pmin:
				curr     = allocator.alloc<Binary>();
				curr->op = PMinVecF32x4;
				break;
			case BinaryConsts::F32x4Pmax:
				curr     = allocator.alloc<Binary>();
				curr->op = PMaxVecF32x4;
				break;
			case BinaryConsts::F64x2Add:
				curr     = allocator.alloc<Binary>();
				curr->op = AddVecF64x2;
				break;
			case BinaryConsts::F64x2Sub:
				curr     = allocator.alloc<Binary>();
				curr->op = SubVecF64x2;
				break;
			case BinaryConsts::F64x2Mul:
				curr     = allocator.alloc<Binary>();
				curr->op = MulVecF64x2;
				break;
			case BinaryConsts::F64x2Div:
				curr     = allocator.alloc<Binary>();
				curr->op = DivVecF64x2;
				break;
			case BinaryConsts::F64x2Min:
				curr     = allocator.alloc<Binary>();
				curr->op = MinVecF64x2;
				break;
			case BinaryConsts::F64x2Max:
				curr     = allocator.alloc<Binary>();
				curr->op = MaxVecF64x2;
				break;
			case BinaryConsts::F64x2Pmin:
				curr     = allocator.alloc<Binary>();
				curr->op = PMinVecF64x2;
				break;
			case BinaryConsts::F64x2Pmax:
				curr     = allocator.alloc<Binary>();
				curr->op = PMaxVecF64x2;
				break;
			case BinaryConsts::I8x16NarrowI16x8S:
				curr     = allocator.alloc<Binary>();
				curr->op = NarrowSVecI16x8ToVecI8x16;
				break;
			case BinaryConsts::I8x16NarrowI16x8U:
				curr     = allocator.alloc<Binary>();
				curr->op = NarrowUVecI16x8ToVecI8x16;
				break;
			case BinaryConsts::I16x8NarrowI32x4S:
				curr     = allocator.alloc<Binary>();
				curr->op = NarrowSVecI32x4ToVecI16x8;
				break;
			case BinaryConsts::I16x8NarrowI32x4U:
				curr     = allocator.alloc<Binary>();
				curr->op = NarrowUVecI32x4ToVecI16x8;
				break;
			case BinaryConsts::I8x16Swizzle:
				curr     = allocator.alloc<Binary>();
				curr->op = SwizzleVecI8x16;
				break;
			case BinaryConsts::I8x16RelaxedSwizzle:
				curr     = allocator.alloc<Binary>();
				curr->op = RelaxedSwizzleVecI8x16;
				break;
			case BinaryConsts::F32x4RelaxedMin:
				curr     = allocator.alloc<Binary>();
				curr->op = RelaxedMinVecF32x4;
				break;
			case BinaryConsts::F32x4RelaxedMax:
				curr     = allocator.alloc<Binary>();
				curr->op = RelaxedMaxVecF32x4;
				break;
			case BinaryConsts::F64x2RelaxedMin:
				curr     = allocator.alloc<Binary>();
				curr->op = RelaxedMinVecF64x2;
				break;
			case BinaryConsts::F64x2RelaxedMax:
				curr     = allocator.alloc<Binary>();
				curr->op = RelaxedMaxVecF64x2;
				break;
			case BinaryConsts::I16x8RelaxedQ15MulrS:
				curr     = allocator.alloc<Binary>();
				curr->op = RelaxedQ15MulrSVecI16x8;
				break;
			case BinaryConsts::I16x8DotI8x16I7x16S:
				curr     = allocator.alloc<Binary>();
				curr->op = DotI8x16I7x16SToVecI16x8;
				break;
			default:
				return false;
			}
			BYN_TRACE("zz node: Binary\n");
			curr->right = popNonVoidExpression();
			curr->left  = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}
		bool OptimizedWasmReader::maybeVisitSIMDUnary(Expression*& out, uint32_t code) {
			Unary* curr;
			switch(code) {
			case BinaryConsts::I8x16Splat:
				curr     = allocator.alloc<Unary>();
				curr->op = SplatVecI8x16;
				break;
			case BinaryConsts::I16x8Splat:
				curr     = allocator.alloc<Unary>();
				curr->op = SplatVecI16x8;
				break;
			case BinaryConsts::I32x4Splat:
				curr     = allocator.alloc<Unary>();
				curr->op = SplatVecI32x4;
				break;
			case BinaryConsts::I64x2Splat:
				curr     = allocator.alloc<Unary>();
				curr->op = SplatVecI64x2;
				break;
			case BinaryConsts::F32x4Splat:
				curr     = allocator.alloc<Unary>();
				curr->op = SplatVecF32x4;
				break;
			case BinaryConsts::F64x2Splat:
				curr     = allocator.alloc<Unary>();
				curr->op = SplatVecF64x2;
				break;
			case BinaryConsts::V128Not:
				curr     = allocator.alloc<Unary>();
				curr->op = NotVec128;
				break;
			case BinaryConsts::V128AnyTrue:
				curr     = allocator.alloc<Unary>();
				curr->op = AnyTrueVec128;
				break;
			case BinaryConsts::I8x16Popcnt:
				curr     = allocator.alloc<Unary>();
				curr->op = PopcntVecI8x16;
				break;
			case BinaryConsts::I8x16Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsVecI8x16;
				break;
			case BinaryConsts::I8x16Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegVecI8x16;
				break;
			case BinaryConsts::I8x16AllTrue:
				curr     = allocator.alloc<Unary>();
				curr->op = AllTrueVecI8x16;
				break;
			case BinaryConsts::I8x16Bitmask:
				curr     = allocator.alloc<Unary>();
				curr->op = BitmaskVecI8x16;
				break;
			case BinaryConsts::I16x8Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsVecI16x8;
				break;
			case BinaryConsts::I16x8Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegVecI16x8;
				break;
			case BinaryConsts::I16x8AllTrue:
				curr     = allocator.alloc<Unary>();
				curr->op = AllTrueVecI16x8;
				break;
			case BinaryConsts::I16x8Bitmask:
				curr     = allocator.alloc<Unary>();
				curr->op = BitmaskVecI16x8;
				break;
			case BinaryConsts::I32x4Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsVecI32x4;
				break;
			case BinaryConsts::I32x4Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegVecI32x4;
				break;
			case BinaryConsts::I32x4AllTrue:
				curr     = allocator.alloc<Unary>();
				curr->op = AllTrueVecI32x4;
				break;
			case BinaryConsts::I32x4Bitmask:
				curr     = allocator.alloc<Unary>();
				curr->op = BitmaskVecI32x4;
				break;
			case BinaryConsts::I64x2Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsVecI64x2;
				break;
			case BinaryConsts::I64x2Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegVecI64x2;
				break;
			case BinaryConsts::I64x2AllTrue:
				curr     = allocator.alloc<Unary>();
				curr->op = AllTrueVecI64x2;
				break;
			case BinaryConsts::I64x2Bitmask:
				curr     = allocator.alloc<Unary>();
				curr->op = BitmaskVecI64x2;
				break;
			case BinaryConsts::F32x4Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsVecF32x4;
				break;
			case BinaryConsts::F32x4Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegVecF32x4;
				break;
			case BinaryConsts::F32x4Sqrt:
				curr     = allocator.alloc<Unary>();
				curr->op = SqrtVecF32x4;
				break;
			case BinaryConsts::F32x4Ceil:
				curr     = allocator.alloc<Unary>();
				curr->op = CeilVecF32x4;
				break;
			case BinaryConsts::F32x4Floor:
				curr     = allocator.alloc<Unary>();
				curr->op = FloorVecF32x4;
				break;
			case BinaryConsts::F32x4Trunc:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncVecF32x4;
				break;
			case BinaryConsts::F32x4Nearest:
				curr     = allocator.alloc<Unary>();
				curr->op = NearestVecF32x4;
				break;
			case BinaryConsts::F64x2Abs:
				curr     = allocator.alloc<Unary>();
				curr->op = AbsVecF64x2;
				break;
			case BinaryConsts::F64x2Neg:
				curr     = allocator.alloc<Unary>();
				curr->op = NegVecF64x2;
				break;
			case BinaryConsts::F64x2Sqrt:
				curr     = allocator.alloc<Unary>();
				curr->op = SqrtVecF64x2;
				break;
			case BinaryConsts::F64x2Ceil:
				curr     = allocator.alloc<Unary>();
				curr->op = CeilVecF64x2;
				break;
			case BinaryConsts::F64x2Floor:
				curr     = allocator.alloc<Unary>();
				curr->op = FloorVecF64x2;
				break;
			case BinaryConsts::F64x2Trunc:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncVecF64x2;
				break;
			case BinaryConsts::F64x2Nearest:
				curr     = allocator.alloc<Unary>();
				curr->op = NearestVecF64x2;
				break;
			case BinaryConsts::I16x8ExtaddPairwiseI8x16S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtAddPairwiseSVecI8x16ToI16x8;
				break;
			case BinaryConsts::I16x8ExtaddPairwiseI8x16U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtAddPairwiseUVecI8x16ToI16x8;
				break;
			case BinaryConsts::I32x4ExtaddPairwiseI16x8S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtAddPairwiseSVecI16x8ToI32x4;
				break;
			case BinaryConsts::I32x4ExtaddPairwiseI16x8U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtAddPairwiseUVecI16x8ToI32x4;
				break;
			case BinaryConsts::I32x4TruncSatF32x4S:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatSVecF32x4ToVecI32x4;
				break;
			case BinaryConsts::I32x4TruncSatF32x4U:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatUVecF32x4ToVecI32x4;
				break;
			case BinaryConsts::F32x4ConvertI32x4S:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertSVecI32x4ToVecF32x4;
				break;
			case BinaryConsts::F32x4ConvertI32x4U:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertUVecI32x4ToVecF32x4;
				break;
			case BinaryConsts::I16x8ExtendLowI8x16S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendLowSVecI8x16ToVecI16x8;
				break;
			case BinaryConsts::I16x8ExtendHighI8x16S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendHighSVecI8x16ToVecI16x8;
				break;
			case BinaryConsts::I16x8ExtendLowI8x16U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendLowUVecI8x16ToVecI16x8;
				break;
			case BinaryConsts::I16x8ExtendHighI8x16U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendHighUVecI8x16ToVecI16x8;
				break;
			case BinaryConsts::I32x4ExtendLowI16x8S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendLowSVecI16x8ToVecI32x4;
				break;
			case BinaryConsts::I32x4ExtendHighI16x8S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendHighSVecI16x8ToVecI32x4;
				break;
			case BinaryConsts::I32x4ExtendLowI16x8U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendLowUVecI16x8ToVecI32x4;
				break;
			case BinaryConsts::I32x4ExtendHighI16x8U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendHighUVecI16x8ToVecI32x4;
				break;
			case BinaryConsts::I64x2ExtendLowI32x4S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendLowSVecI32x4ToVecI64x2;
				break;
			case BinaryConsts::I64x2ExtendHighI32x4S:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendHighSVecI32x4ToVecI64x2;
				break;
			case BinaryConsts::I64x2ExtendLowI32x4U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendLowUVecI32x4ToVecI64x2;
				break;
			case BinaryConsts::I64x2ExtendHighI32x4U:
				curr     = allocator.alloc<Unary>();
				curr->op = ExtendHighUVecI32x4ToVecI64x2;
				break;
			case BinaryConsts::F64x2ConvertLowI32x4S:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertLowSVecI32x4ToVecF64x2;
				break;
			case BinaryConsts::F64x2ConvertLowI32x4U:
				curr     = allocator.alloc<Unary>();
				curr->op = ConvertLowUVecI32x4ToVecF64x2;
				break;
			case BinaryConsts::I32x4TruncSatF64x2SZero:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatZeroSVecF64x2ToVecI32x4;
				break;
			case BinaryConsts::I32x4TruncSatF64x2UZero:
				curr     = allocator.alloc<Unary>();
				curr->op = TruncSatZeroUVecF64x2ToVecI32x4;
				break;
			case BinaryConsts::F32x4DemoteF64x2Zero:
				curr     = allocator.alloc<Unary>();
				curr->op = DemoteZeroVecF64x2ToVecF32x4;
				break;
			case BinaryConsts::F64x2PromoteLowF32x4:
				curr     = allocator.alloc<Unary>();
				curr->op = PromoteLowVecF32x4ToVecF64x2;
				break;
			case BinaryConsts::I32x4RelaxedTruncF32x4S:
				curr     = allocator.alloc<Unary>();
				curr->op = RelaxedTruncSVecF32x4ToVecI32x4;
				break;
			case BinaryConsts::I32x4RelaxedTruncF32x4U:
				curr     = allocator.alloc<Unary>();
				curr->op = RelaxedTruncUVecF32x4ToVecI32x4;
				break;
			case BinaryConsts::I32x4RelaxedTruncF64x2SZero:
				curr     = allocator.alloc<Unary>();
				curr->op = RelaxedTruncZeroSVecF64x2ToVecI32x4;
				break;
			case BinaryConsts::I32x4RelaxedTruncF64x2UZero:
				curr     = allocator.alloc<Unary>();
				curr->op = RelaxedTruncZeroUVecF64x2ToVecI32x4;
				break;
			default:
				return false;
			}
			curr->value = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDConst(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::V128Const) {
				return false;
			}
			auto* curr  = allocator.alloc<Const>();
			curr->value = getValue<ValueWritten::ConstV128>();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDStore(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::V128Store) {
				return false;
			}
			auto* curr      = allocator.alloc<Store>();
			curr->bytes     = 16;
			curr->valueType = Type::v128;
			readMemoryAccess(curr->align, curr->offset);
			curr->isAtomic = false;
			curr->value    = popNonVoidExpression();
			curr->ptr      = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDExtract(Expression*& out, uint32_t code) {
			SIMDExtract* curr;
			switch(code) {
			case BinaryConsts::I8x16ExtractLaneS:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneSVecI8x16;
				curr->index = getValue<ValueWritten::SIMDIndex>(16);
				break;
			case BinaryConsts::I8x16ExtractLaneU:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneUVecI8x16;
				curr->index = getValue<ValueWritten::SIMDIndex>(16);
				break;
			case BinaryConsts::I16x8ExtractLaneS:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneSVecI16x8;
				curr->index = getValue<ValueWritten::SIMDIndex>(16);
				break;
			case BinaryConsts::I16x8ExtractLaneU:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneUVecI16x8;
				curr->index = getValue<ValueWritten::SIMDIndex>(8);
				break;
			case BinaryConsts::I32x4ExtractLane:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneVecI32x4;
				curr->index = getValue<ValueWritten::SIMDIndex>(4);
				break;
			case BinaryConsts::I64x2ExtractLane:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneVecI64x2;
				curr->index = getValue<ValueWritten::SIMDIndex>(2);
				break;
			case BinaryConsts::F32x4ExtractLane:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneVecF32x4;
				curr->index = getValue<ValueWritten::SIMDIndex>(4);
				break;
			case BinaryConsts::F64x2ExtractLane:
				curr        = allocator.alloc<SIMDExtract>();
				curr->op    = ExtractLaneVecF64x2;
				curr->index = getValue<ValueWritten::SIMDIndex>(2);
				break;
			default:
				return false;
			}
			curr->vec = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDReplace(Expression*& out, uint32_t code) {
			SIMDReplace* curr;
			switch(code) {
			case BinaryConsts::I8x16ReplaceLane:
				curr        = allocator.alloc<SIMDReplace>();
				curr->op    = ReplaceLaneVecI8x16;
				curr->index = getValue<ValueWritten::SIMDIndex>(16);
				break;
			case BinaryConsts::I16x8ReplaceLane:
				curr        = allocator.alloc<SIMDReplace>();
				curr->op    = ReplaceLaneVecI16x8;
				curr->index = getValue<ValueWritten::SIMDIndex>(8);
				break;
			case BinaryConsts::I32x4ReplaceLane:
				curr        = allocator.alloc<SIMDReplace>();
				curr->op    = ReplaceLaneVecI32x4;
				curr->index = getValue<ValueWritten::SIMDIndex>(4);
				break;
			case BinaryConsts::I64x2ReplaceLane:
				curr        = allocator.alloc<SIMDReplace>();
				curr->op    = ReplaceLaneVecI64x2;
				curr->index = getValue<ValueWritten::SIMDIndex>(2);
				break;
			case BinaryConsts::F32x4ReplaceLane:
				curr        = allocator.alloc<SIMDReplace>();
				curr->op    = ReplaceLaneVecF32x4;
				curr->index = getValue<ValueWritten::SIMDIndex>(4);
				break;
			case BinaryConsts::F64x2ReplaceLane:
				curr        = allocator.alloc<SIMDReplace>();
				curr->op    = ReplaceLaneVecF64x2;
				curr->index = getValue<ValueWritten::SIMDIndex>(2);
				break;
			default:
				return false;
			}
			curr->value = popNonVoidExpression();
			curr->vec   = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDShuffle(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::I8x16Shuffle) {
				return false;
			}
			auto* curr = allocator.alloc<SIMDShuffle>();
			for(auto i = 0; i < 16; ++i) {
				curr->mask[i] = getValue<ValueWritten::SIMDIndex>(32);
			}
			curr->right = popNonVoidExpression();
			curr->left  = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDTernary(Expression*& out, uint32_t code) {
			SIMDTernary* curr;
			switch(code) {
			case BinaryConsts::V128Bitselect:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = Bitselect;
				break;
			case BinaryConsts::I8x16Laneselect:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = LaneselectI8x16;
				break;
			case BinaryConsts::I16x8Laneselect:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = LaneselectI16x8;
				break;
			case BinaryConsts::I32x4Laneselect:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = LaneselectI32x4;
				break;
			case BinaryConsts::I64x2Laneselect:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = LaneselectI64x2;
				break;
			case BinaryConsts::F32x4RelaxedFma:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = RelaxedFmaVecF32x4;
				break;
			case BinaryConsts::F32x4RelaxedFms:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = RelaxedFmsVecF32x4;
				break;
			case BinaryConsts::F64x2RelaxedFma:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = RelaxedFmaVecF64x2;
				break;
			case BinaryConsts::F64x2RelaxedFms:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = RelaxedFmsVecF64x2;
				break;
			case BinaryConsts::I32x4DotI8x16I7x16AddS:
				curr     = allocator.alloc<SIMDTernary>();
				curr->op = DotI8x16I7x16AddSToVecI32x4;
				break;
			default:
				return false;
			}
			curr->c = popNonVoidExpression();
			curr->b = popNonVoidExpression();
			curr->a = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDShift(Expression*& out, uint32_t code) {
			SIMDShift* curr;
			switch(code) {
			case BinaryConsts::I8x16Shl:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShlVecI8x16;
				break;
			case BinaryConsts::I8x16ShrS:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrSVecI8x16;
				break;
			case BinaryConsts::I8x16ShrU:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrUVecI8x16;
				break;
			case BinaryConsts::I16x8Shl:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShlVecI16x8;
				break;
			case BinaryConsts::I16x8ShrS:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrSVecI16x8;
				break;
			case BinaryConsts::I16x8ShrU:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrUVecI16x8;
				break;
			case BinaryConsts::I32x4Shl:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShlVecI32x4;
				break;
			case BinaryConsts::I32x4ShrS:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrSVecI32x4;
				break;
			case BinaryConsts::I32x4ShrU:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrUVecI32x4;
				break;
			case BinaryConsts::I64x2Shl:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShlVecI64x2;
				break;
			case BinaryConsts::I64x2ShrS:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrSVecI64x2;
				break;
			case BinaryConsts::I64x2ShrU:
				curr     = allocator.alloc<SIMDShift>();
				curr->op = ShrUVecI64x2;
				break;
			default:
				return false;
			}
			curr->shift = popNonVoidExpression();
			curr->vec   = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDLoad(Expression*& out, uint32_t code) {
			if(code == BinaryConsts::V128Load) {
				auto* curr  = allocator.alloc<Load>();
				curr->type  = Type::v128;
				curr->bytes = 16;
				readMemoryAccess(curr->align, curr->offset);
				curr->isAtomic = false;
				curr->ptr      = popNonVoidExpression();
				curr->finalize();
				out = curr;
				return true;
			}
			SIMDLoad* curr;
			switch(code) {
			case BinaryConsts::V128Load8Splat:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load8SplatVec128;
				break;
			case BinaryConsts::V128Load16Splat:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load16SplatVec128;
				break;
			case BinaryConsts::V128Load32Splat:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load32SplatVec128;
				break;
			case BinaryConsts::V128Load64Splat:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load64SplatVec128;
				break;
			case BinaryConsts::V128Load8x8S:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load8x8SVec128;
				break;
			case BinaryConsts::V128Load8x8U:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load8x8UVec128;
				break;
			case BinaryConsts::V128Load16x4S:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load16x4SVec128;
				break;
			case BinaryConsts::V128Load16x4U:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load16x4UVec128;
				break;
			case BinaryConsts::V128Load32x2S:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load32x2SVec128;
				break;
			case BinaryConsts::V128Load32x2U:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load32x2UVec128;
				break;
			case BinaryConsts::V128Load32Zero:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load32ZeroVec128;
				break;
			case BinaryConsts::V128Load64Zero:
				curr     = allocator.alloc<SIMDLoad>();
				curr->op = Load64ZeroVec128;
				break;
			default:
				return false;
			}
			readMemoryAccess(curr->align, curr->offset);
			curr->ptr = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitSIMDLoadStoreLane(Expression*& out, uint32_t code) {
			SIMDLoadStoreLaneOp op;
			size_t lanes;
			switch(code) {
			case BinaryConsts::V128Load8Lane:
				op    = Load8LaneVec128;
				lanes = 16;
				break;
			case BinaryConsts::V128Load16Lane:
				op    = Load16LaneVec128;
				lanes = 8;
				break;
			case BinaryConsts::V128Load32Lane:
				op    = Load32LaneVec128;
				lanes = 4;
				break;
			case BinaryConsts::V128Load64Lane:
				op    = Load64LaneVec128;
				lanes = 2;
				break;
			case BinaryConsts::V128Store8Lane:
				op    = Store8LaneVec128;
				lanes = 16;
				break;
			case BinaryConsts::V128Store16Lane:
				op    = Store16LaneVec128;
				lanes = 8;
				break;
			case BinaryConsts::V128Store32Lane:
				op    = Store32LaneVec128;
				lanes = 4;
				break;
			case BinaryConsts::V128Store64Lane:
				op    = Store64LaneVec128;
				lanes = 2;
				break;
			default:
				return false;
			}
			auto* curr = allocator.alloc<SIMDLoadStoreLane>();
			curr->op   = op;
			readMemoryAccess(curr->align, curr->offset);
			curr->index = getValue<ValueWritten::SIMDIndex>(lanes);
			curr->vec   = popNonVoidExpression();
			curr->ptr   = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		void OptimizedWasmReader::visitSelect(Select* curr, uint8_t code) {
			BYN_TRACE("zz node: Select, code " << int32_t(code) << std::endl);
			if(code == BinaryConsts::SelectWithType) {
				size_t numTypes = getValue<ValueWritten::NumSelectTypes>();
				std::vector<Type> types;
				for(size_t i = 0; i < numTypes; i++) {
					types.push_back(getType());
				}
				curr->type = Type(types);
			}
			curr->condition = popNonVoidExpression();
			curr->ifFalse   = popNonVoidExpression();
			curr->ifTrue    = popNonVoidExpression();
			if(code == BinaryConsts::SelectWithType) {
				curr->finalize(curr->type);
			} else {
				curr->finalize();
			}
		}

		void OptimizedWasmReader::visitReturn(Return* curr) {
			BYN_TRACE("zz node: Return\n");
			requireFunctionContext("return");
			Type type = currFunction->getResults();
			if(type.isConcrete()) {
				curr->value = popTypedExpression(type);
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitMemorySize(MemorySize* curr) {
			BYN_TRACE("zz node: MemorySize\n");
			auto reserved = getValue<ValueWritten::MemorySizeFlags>();
			if(reserved != 0) {
				throwError("Invalid reserved field on memory.size");
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitMemoryGrow(MemoryGrow* curr) {
			BYN_TRACE("zz node: MemoryGrow\n");
			curr->delta   = popNonVoidExpression();
			auto reserved = getValue<ValueWritten::MemoryGrowFlags>();
			if(reserved != 0) {
				throwError("Invalid reserved field on memory.grow");
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitNop(Nop* curr) {
			BYN_TRACE("zz node: Nop\n");
		}

		void OptimizedWasmReader::visitUnreachable(Unreachable* curr) {
			BYN_TRACE("zz node: Unreachable\n");
		}

		void OptimizedWasmReader::visitDrop(Drop* curr) {
			BYN_TRACE("zz node: Drop\n");
			curr->value = popNonVoidExpression();
			curr->finalize();
		}

		void OptimizedWasmReader::visitRefNull(RefNull* curr) {
			BYN_TRACE("zz node: RefNull\n");
			curr->finalize(getHeapType());
		}

		void OptimizedWasmReader::visitRefIs(RefIs* curr, uint8_t code) {
			BYN_TRACE("zz node: RefIs\n");
			switch(code) {
			case BinaryConsts::RefIsNull:
				curr->op = RefIsNull;
				break;
			case BinaryConsts::RefIsFunc:
				curr->op = RefIsFunc;
				break;
			case BinaryConsts::RefIsData:
				curr->op = RefIsData;
				break;
			case BinaryConsts::RefIsI31:
				curr->op = RefIsI31;
				break;
			default:
				WASM_UNREACHABLE("invalid code for ref.is_*");
			}
			curr->value = popNonVoidExpression();
			curr->finalize();
		}

		void OptimizedWasmReader::visitRefFunc(RefFunc* curr) {
			BYN_TRACE("zz node: RefFunc\n");
			Index index = getValue<ValueWritten::FunctionIndex>();
			// We don't know function names yet, so record this use to be updated later.
			// Note that we do not need to check that 'index' is in bounds, as that will
			// be verified in the next line. (Also, note that functionRefs[index] may
			// write to an odd place in the functionRefs map if index is invalid, but that
			// is harmless.)
			functionRefs[index].push_back(curr);
			// To support typed function refs, we give the reference not just a general
			// funcref, but a specific subtype with the actual signature.
			curr->finalize(Type(getTypeByFunctionIndex(index), NonNullable));
		}

		void OptimizedWasmReader::visitRefEq(RefEq* curr) {
			BYN_TRACE("zz node: RefEq\n");
			curr->right = popNonVoidExpression();
			curr->left  = popNonVoidExpression();
			curr->finalize();
		}

		void OptimizedWasmReader::visitTableGet(TableGet* curr) {
			BYN_TRACE("zz node: TableGet\n");
			Index tableIdx = getValue<ValueWritten::TableIndex>();
			if(tableIdx >= tables.size()) {
				throwError("bad table index");
			}
			curr->index = popNonVoidExpression();
			curr->type  = tables[tableIdx]->type;
			curr->finalize();
			// Defer setting the table name for later, when we know it.
			tableRefs[tableIdx].push_back(curr);
		}

		void OptimizedWasmReader::visitTableSet(TableSet* curr) {
			BYN_TRACE("zz node: TableSet\n");
			Index tableIdx = getValue<ValueWritten::TableIndex>();
			if(tableIdx >= tables.size()) {
				throwError("bad table index");
			}
			curr->value = popNonVoidExpression();
			curr->index = popNonVoidExpression();
			curr->finalize();
			// Defer setting the table name for later, when we know it.
			tableRefs[tableIdx].push_back(curr);
		}

		void OptimizedWasmReader::visitTryOrTryInBlock(Expression*& out) {
			BYN_TRACE("zz node: Try\n");
			auto* curr = allocator.alloc<Try>();
			startControlFlow(curr);
			// For simplicity of implementation, like if scopes, we create a hidden block
			// within each try-body and catch-body, and let branches target those inner
			// blocks instead.
			curr->type = getType();
			curr->body = getBlockOrSingleton(curr->type);

			Builder builder(wasm);
			// A nameless label shared by all catch body blocks
			Name catchLabel = getNextLabel();
			breakStack.push_back({ catchLabel, curr->type });

			auto readCatchBody = [&](Type tagType) {
				auto start = expressionStack.size();
				if(tagType != Type::none) {
					pushExpression(builder.makePop(tagType));
				}
				processExpressions();
				size_t end = expressionStack.size();
				if(start > end) {
					throwError("block cannot pop from outside");
				}
				if(end - start == 1) {
					curr->catchBodies.push_back(popExpression());
				} else {
					auto* block = allocator.alloc<Block>();
					pushBlockElements(block, curr->type, start);
					block->finalize(curr->type);
					curr->catchBodies.push_back(block);
				}
			};

			while(lastSeparator == BinaryConsts::Catch || lastSeparator == BinaryConsts::CatchAll) {
				if(lastSeparator == BinaryConsts::Catch) {
					auto index = getValue<ValueWritten::TagIndex>();
					if(index >= wasm.tags.size()) {
						throwError("bad tag index");
					}
					auto* tag = wasm.tags[index].get();
					curr->catchTags.push_back(tag->name);
					readCatchBody(tag->sig.params);

				} else { // catch_all
					if(curr->hasCatchAll()) {
						throwError("there should be at most one 'catch_all' clause per try");
					}
					readCatchBody(Type::none);
				}
			}
			breakStack.pop_back();

			if(lastSeparator == BinaryConsts::Delegate) {
				curr->delegateTarget = getExceptionTargetName(getValue<ValueWritten::BreakIndex>());
			}

			// For simplicity, we ensure that try's labels can only be targeted by
			// delegates and rethrows, and delegates/rethrows can only target try's
			// labels. (If they target blocks or loops, it is a validation failure.)
			// Because we create an inner block within each try and catch body, if any
			// delegate/rethrow targets those inner blocks, we should make them target the
			// try's label instead.
			curr->name = getNextLabel();
			if(auto* block = curr->body->dynCast<Block>()) {
				if(block->name.is()) {
					if(exceptionTargetNames.find(block->name) != exceptionTargetNames.end()) {
						BranchUtils::replaceExceptionTargets(block, block->name, curr->name);
						exceptionTargetNames.erase(block->name);
					}
				}
			}
			if(exceptionTargetNames.find(catchLabel) != exceptionTargetNames.end()) {
				for(auto* catchBody : curr->catchBodies) {
					BranchUtils::replaceExceptionTargets(catchBody, catchLabel, curr->name);
				}
				exceptionTargetNames.erase(catchLabel);
			}

			// If catch bodies contained stacky code, 'pop's can be nested within a block.
			// Fix that up.
			EHUtils::handleBlockNestedPop(curr, currFunction, wasm);
			curr->finalize(curr->type);

			// For simplicity, we create an inner block within the catch body too, but the
			// one within the 'catch' *must* be omitted when we write out the binary back
			// later, because the 'catch' instruction pushes a value onto the stack and
			// the inner block does not support block input parameters without multivalue
			// support.
			// try
			//   ...
			// catch $e   ;; Pushes value(s) onto the stack
			//   block  ;; Inner block. Should be deleted when writing binary!
			//     use the pushed value
			//   end
			// end
			//
			// But when input binary code is like
			// try
			//   ...
			// catch $e
			//   br 0
			// end
			//
			// 'br 0' accidentally happens to target the inner block, creating code like
			// this in Binaryen IR, making the inner block not deletable, resulting in a
			// validation error:
			// (try
			//   ...
			//   (catch $e
			//     (block $label0 ;; Cannot be deleted, because there's a branch to this
			//       ...
			//       (br $label0)
			//     )
			//   )
			// )
			//
			// When this happens, we fix this by creating a block that wraps the whole
			// try-catch, and making the branches target that block instead, like this:
			// (block $label  ;; New enclosing block, new target for the branch
			//   (try
			//     ...
			//     (catch $e
			//       (block   ;; Now this can be deleted when writing binary
			//         ...
			//         (br $label)
			//       )
			//     )
			//   )
			// )
			if(breakTargetNames.find(catchLabel) == breakTargetNames.end()) {
				out = curr;
			} else {
				// Create a new block that encloses the whole try-catch
				auto* block = builder.makeBlock(catchLabel, curr);
				out         = block;
			}
			breakTargetNames.erase(catchLabel);
		}

		void OptimizedWasmReader::visitThrow(Throw* curr) {
			BYN_TRACE("zz node: Throw\n");
			auto index = getValue<ValueWritten::TagIndex>();
			if(index >= wasm.tags.size()) {
				throwError("bad tag index");
			}
			auto* tag  = wasm.tags[index].get();
			curr->tag  = tag->name;
			size_t num = tag->sig.params.size();
			curr->operands.resize(num);
			for(size_t i = 0; i < num; i++) {
				curr->operands[num - i - 1] = popNonVoidExpression();
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitRethrow(Rethrow* curr) {
			BYN_TRACE("zz node: Rethrow\n");
			curr->target = getExceptionTargetName(getValue<ValueWritten::BreakIndex>());
			// This special target is valid only for delegates
			if(curr->target == DELEGATE_CALLER_TARGET) {
				throwError(std::string("rethrow target cannot use internal name ") + DELEGATE_CALLER_TARGET.str);
			}
			curr->finalize();
		}

		void OptimizedWasmReader::visitCallRef(CallRef* curr) {
			BYN_TRACE("zz node: CallRef\n");
			curr->target = popNonVoidExpression();
			auto type    = curr->target->type;
			if(type == Type::unreachable) {
				// If our input is unreachable, then we cannot even find out how many inputs
				// we have, and just set ourselves to unreachable as well.
				curr->finalize(type);
				return;
			}
			if(!type.isRef()) {
				throwError("Non-ref type for a call_ref: " + type.toString());
			}
			auto heapType = type.getHeapType();
			if(!heapType.isSignature()) {
				throwError("Invalid reference type for a call_ref: " + type.toString());
			}
			auto sig = heapType.getSignature();
			auto num = sig.params.size();
			curr->operands.resize(num);
			for(size_t i = 0; i < num; i++) {
				curr->operands[num - i - 1] = popNonVoidExpression();
			}
			curr->finalize(sig.results);
		}

		void OptimizedWasmReader::visitLet(Block* curr) {
			// A let is lowered into a block that contains the value, and we allocate
			// locals as needed, which works as we remove non-nullability.

			startControlFlow(curr);
			// Get the output type.
			curr->type = getType();
			// Get the new local types. First, get the absolute index from which we will
			// start to allocate them.
			requireFunctionContext("let");
			Index absoluteStart = currFunction->vars.size();
			readVars();
			Index numNewVars = currFunction->vars.size() - absoluteStart;
			// Assign the values into locals.
			Builder builder(wasm);
			for(Index i = 0; i < numNewVars; i++) {
				auto* value = popNonVoidExpression();
				curr->list.push_back(builder.makeLocalSet(absoluteStart + i, value));
			}
			// Read the body, with adjusted local indexes.
			letStack.emplace_back(LetData { numNewVars, absoluteStart });
			curr->list.push_back(getBlockOrSingleton(curr->type));
			letStack.pop_back();
			curr->finalize(curr->type);
		}

		bool OptimizedWasmReader::maybeVisitI31New(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::I31New) {
				return false;
			}
			auto* curr  = allocator.alloc<I31New>();
			curr->value = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitI31Get(Expression*& out, uint32_t code) {
			I31Get* curr;
			switch(code) {
			case BinaryConsts::I31GetS:
				curr          = allocator.alloc<I31Get>();
				curr->signed_ = true;
				break;
			case BinaryConsts::I31GetU:
				curr          = allocator.alloc<I31Get>();
				curr->signed_ = false;
				break;
			default:
				return false;
			}
			curr->i31 = popNonVoidExpression();
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitRefTest(Expression*& out, uint32_t code) {
			if(code == BinaryConsts::RefTest) {
				auto* rtt = popNonVoidExpression();
				auto* ref = popNonVoidExpression();
				out       = Builder(wasm).makeRefTest(ref, rtt);
				return true;
			} else if(code == BinaryConsts::RefTestStatic) {
				auto intendedType = getIndexedHeapType();
				auto* ref         = popNonVoidExpression();
				out               = Builder(wasm).makeRefTest(ref, intendedType);
				return true;
			}
			return false;
		}

		bool OptimizedWasmReader::maybeVisitRefCast(Expression*& out, uint32_t code) {
			if(code == BinaryConsts::RefCast) {
				auto* rtt = popNonVoidExpression();
				auto* ref = popNonVoidExpression();
				out       = Builder(wasm).makeRefCast(ref, rtt);
				return true;
			} else if(code == BinaryConsts::RefCastStatic || code == BinaryConsts::RefCastNopStatic) {
				auto intendedType = getIndexedHeapType();
				auto* ref         = popNonVoidExpression();
				auto safety       = code == BinaryConsts::RefCastNopStatic ? RefCast::Unsafe : RefCast::Safe;
				out               = Builder(wasm).makeRefCast(ref, intendedType, safety);
				return true;
			}
			return false;
		}

		bool OptimizedWasmReader::maybeVisitBrOn(Expression*& out, uint32_t code) {
			BrOnOp op;
			switch(code) {
			case BinaryConsts::BrOnNull:
				op = BrOnNull;
				break;
			case BinaryConsts::BrOnNonNull:
				op = BrOnNonNull;
				break;
			case BinaryConsts::BrOnCast:
			case BinaryConsts::BrOnCastStatic:
				op = BrOnCast;
				break;
			case BinaryConsts::BrOnCastFail:
			case BinaryConsts::BrOnCastStaticFail:
				op = BrOnCastFail;
				break;
			case BinaryConsts::BrOnFunc:
				op = BrOnFunc;
				break;
			case BinaryConsts::BrOnNonFunc:
				op = BrOnNonFunc;
				break;
			case BinaryConsts::BrOnData:
				op = BrOnData;
				break;
			case BinaryConsts::BrOnNonData:
				op = BrOnNonData;
				break;
			case BinaryConsts::BrOnI31:
				op = BrOnI31;
				break;
			case BinaryConsts::BrOnNonI31:
				op = BrOnNonI31;
				break;
			default:
				return false;
			}
			auto name = getBreakTarget(getValue<ValueWritten::BreakIndex>()).name;
			if(code == BinaryConsts::BrOnCastStatic || code == BinaryConsts::BrOnCastStaticFail) {
				auto intendedType = getIndexedHeapType();
				auto* ref         = popNonVoidExpression();
				out               = Builder(wasm).makeBrOn(op, name, ref, intendedType);
				return true;
			}
			Expression* rtt = nullptr;
			if(op == BrOnCast || op == BrOnCastFail) {
				rtt = popNonVoidExpression();
			}
			auto* ref = popNonVoidExpression();
			out       = ValidatingBuilder(wasm, binaryIO.getPos()).validateAndMakeBrOn(op, name, ref, rtt);
			return true;
		}

		bool OptimizedWasmReader::maybeVisitRttCanon(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::RttCanon) {
				return false;
			}
			auto heapType = getIndexedHeapType();
			out           = Builder(wasm).makeRttCanon(heapType);
			return true;
		}

		bool OptimizedWasmReader::maybeVisitRttSub(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::RttSub && code != BinaryConsts::RttFreshSub) {
				return false;
			}
			auto targetHeapType = getIndexedHeapType();
			auto* parent        = popNonVoidExpression();
			if(code == BinaryConsts::RttSub) {
				out = Builder(wasm).makeRttSub(targetHeapType, parent);
			} else {
				out = Builder(wasm).makeRttFreshSub(targetHeapType, parent);
			}
			return true;
		}

		bool OptimizedWasmReader::maybeVisitStructNew(Expression*& out, uint32_t code) {
			if(code == BinaryConsts::StructNew || code == BinaryConsts::StructNewDefault) {
				auto heapType = getIndexedHeapType();
				std::vector<Expression*> operands;
				if(code == BinaryConsts::StructNew) {
					auto numOperands = heapType.getStruct().fields.size();
					operands.resize(numOperands);
					for(Index i = 0; i < numOperands; i++) {
						operands[numOperands - i - 1] = popNonVoidExpression();
					}
				}
				out = Builder(wasm).makeStructNew(heapType, operands);
				return true;
			} else if(code == BinaryConsts::StructNewWithRtt || code == BinaryConsts::StructNewDefaultWithRtt) {
				auto heapType = getIndexedHeapType();
				auto* rtt     = popNonVoidExpression();
				validateHeapTypeUsingChild(rtt, heapType);
				std::vector<Expression*> operands;
				if(code == BinaryConsts::StructNewWithRtt) {
					auto numOperands = heapType.getStruct().fields.size();
					operands.resize(numOperands);
					for(Index i = 0; i < numOperands; i++) {
						operands[numOperands - i - 1] = popNonVoidExpression();
					}
				}
				out = Builder(wasm).makeStructNew(rtt, operands);
				return true;
			}
			return false;
		}

		bool OptimizedWasmReader::maybeVisitStructGet(Expression*& out, uint32_t code) {
			StructGet* curr;
			switch(code) {
			case BinaryConsts::StructGet:
				curr = allocator.alloc<StructGet>();
				break;
			case BinaryConsts::StructGetS:
				curr          = allocator.alloc<StructGet>();
				curr->signed_ = true;
				break;
			case BinaryConsts::StructGetU:
				curr          = allocator.alloc<StructGet>();
				curr->signed_ = false;
				break;
			default:
				return false;
			}
			auto heapType = getIndexedHeapType();
			curr->index   = getValue<ValueWritten::StructFieldIndex>();
			curr->ref     = popNonVoidExpression();
			validateHeapTypeUsingChild(curr->ref, heapType);
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitStructSet(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::StructSet) {
				return false;
			}
			auto* curr    = allocator.alloc<StructSet>();
			auto heapType = getIndexedHeapType();
			curr->index   = getValue<ValueWritten::StructFieldIndex>();
			curr->value   = popNonVoidExpression();
			curr->ref     = popNonVoidExpression();
			validateHeapTypeUsingChild(curr->ref, heapType);
			curr->finalize();
			out = curr;
			return true;
		}

		bool OptimizedWasmReader::maybeVisitArrayNew(Expression*& out, uint32_t code) {
			if(code == BinaryConsts::ArrayNew || code == BinaryConsts::ArrayNewDefault) {
				auto heapType    = getIndexedHeapType();
				auto* size       = popNonVoidExpression();
				Expression* init = nullptr;
				if(code == BinaryConsts::ArrayNew) {
					init = popNonVoidExpression();
				}
				out = Builder(wasm).makeArrayNew(heapType, size, init);
				return true;
			} else if(code == BinaryConsts::ArrayNewWithRtt || code == BinaryConsts::ArrayNewDefaultWithRtt) {
				auto heapType = getIndexedHeapType();
				auto* rtt     = popNonVoidExpression();
				validateHeapTypeUsingChild(rtt, heapType);
				auto* size       = popNonVoidExpression();
				Expression* init = nullptr;
				if(code == BinaryConsts::ArrayNewWithRtt) {
					init = popNonVoidExpression();
				}
				out = Builder(wasm).makeArrayNew(rtt, size, init);
				return true;
			}
			return false;
		}

		bool OptimizedWasmReader::maybeVisitArrayInit(Expression*& out, uint32_t code) {
			if(code == BinaryConsts::ArrayInitStatic) {
				auto heapType = getIndexedHeapType();
				auto size     = getValue<ValueWritten::ArraySize>();
				std::vector<Expression*> values(size);
				for(size_t i = 0; i < size; i++) {
					values[size - i - 1] = popNonVoidExpression();
				}
				out = Builder(wasm).makeArrayInit(heapType, values);
				return true;
			} else if(code == BinaryConsts::ArrayInit) {
				auto heapType = getIndexedHeapType();
				auto size     = getValue<ValueWritten::ArraySize>();
				auto* rtt     = popNonVoidExpression();
				validateHeapTypeUsingChild(rtt, heapType);
				std::vector<Expression*> values(size);
				for(size_t i = 0; i < size; i++) {
					values[size - i - 1] = popNonVoidExpression();
				}
				out = Builder(wasm).makeArrayInit(rtt, values);
				return true;
			}
			return false;
		}

		bool OptimizedWasmReader::maybeVisitArrayGet(Expression*& out, uint32_t code) {
			bool signed_ = false;
			switch(code) {
			case BinaryConsts::ArrayGet:
			case BinaryConsts::ArrayGetU:
				break;
			case BinaryConsts::ArrayGetS:
				signed_ = true;
				break;
			default:
				return false;
			}
			auto heapType = getIndexedHeapType();
			auto* index   = popNonVoidExpression();
			auto* ref     = popNonVoidExpression();
			validateHeapTypeUsingChild(ref, heapType);
			out = Builder(wasm).makeArrayGet(ref, index, signed_);
			return true;
		}

		bool OptimizedWasmReader::maybeVisitArraySet(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::ArraySet) {
				return false;
			}
			auto heapType = getIndexedHeapType();
			auto* value   = popNonVoidExpression();
			auto* index   = popNonVoidExpression();
			auto* ref     = popNonVoidExpression();
			validateHeapTypeUsingChild(ref, heapType);
			out = Builder(wasm).makeArraySet(ref, index, value);
			return true;
		}

		bool OptimizedWasmReader::maybeVisitArrayLen(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::ArrayLen) {
				return false;
			}
			auto heapType = getIndexedHeapType();
			auto* ref     = popNonVoidExpression();
			validateHeapTypeUsingChild(ref, heapType);
			out = Builder(wasm).makeArrayLen(ref);
			return true;
		}

		bool OptimizedWasmReader::maybeVisitArrayCopy(Expression*& out, uint32_t code) {
			if(code != BinaryConsts::ArrayCopy) {
				return false;
			}
			auto destHeapType = getIndexedHeapType();
			auto srcHeapType  = getIndexedHeapType();
			auto* length      = popNonVoidExpression();
			auto* srcIndex    = popNonVoidExpression();
			auto* srcRef      = popNonVoidExpression();
			auto* destIndex   = popNonVoidExpression();
			auto* destRef     = popNonVoidExpression();
			validateHeapTypeUsingChild(destRef, destHeapType);
			validateHeapTypeUsingChild(srcRef, srcHeapType);
			out = Builder(wasm).makeArrayCopy(destRef, destIndex, srcRef, srcIndex, length);
			return true;
		}

		void OptimizedWasmReader::visitRefAs(RefAs* curr, uint8_t code) {
			BYN_TRACE("zz node: RefAs\n");
			switch(code) {
			case BinaryConsts::RefAsNonNull:
				curr->op = RefAsNonNull;
				break;
			case BinaryConsts::RefAsFunc:
				curr->op = RefAsFunc;
				break;
			case BinaryConsts::RefAsData:
				curr->op = RefAsData;
				break;
			case BinaryConsts::RefAsI31:
				curr->op = RefAsI31;
				break;
			default:
				WASM_UNREACHABLE("invalid code for ref.as_*");
			}
			curr->value = popNonVoidExpression();
			if(!curr->value->type.isRef() && curr->value->type != Type::unreachable) {
				throwError("bad input type for ref.as: " + curr->value->type.toString());
			}
			curr->finalize();
		}

		void OptimizedWasmReader::throwError(std::string text) {
			throw ParseException(text, 0, binaryIO.getPos());
		}

		void OptimizedWasmReader::validateHeapTypeUsingChild(Expression* child, HeapType heapType) {
			if(child->type == Type::unreachable) {
				return;
			}
			if((!child->type.isRef() && !child->type.isRtt())
				|| !HeapType::isSubType(child->type.getHeapType(), heapType)) {
				throwError("bad heap type: expected " + heapType.toString() + " but found " + child->type.toString());
			}
		}

		// emits a node, but if it is a block with no name, emit a list of its contents
		template <typename SubType> void NewBinaryenIRWriter<SubType>::visitPossibleBlockContents(Expression* curr) {
			auto* block = curr->dynCast<Block>();
			if(!block || BranchUtils::BranchSeeker::has(block, block->name)) {
				visit(curr);
				return;
			}
			for(auto* child : block->list) {
				visit(child);
				// Since this child was unreachable, either this child or one of its
				// descendants was a source of unreachability that was actually
				// emitted. Subsequent children won't be reachable, so skip them.
				if(child->type == Type::unreachable) {
					break;
				}
			}
		}

		template <typename SubType> void NewBinaryenIRWriter<SubType>::visit(Expression* curr) {
			emitDebugLocation(curr);
			// We emit unreachable instructions that create unreachability, but not
			// unreachable instructions that just inherit unreachability from their
			// children, since the latter won't be reached. This (together with logic in
			// the control flow visitors) also ensures that the final instruction in each
			// unreachable block is a source of unreachability, which means we don't need
			// to emit an extra `unreachable` before the end of the block to prevent type
			// errors.
			bool hasUnreachableChild = false;
			for(auto* child : ValueChildIterator(curr)) {
				visit(child);
				if(child->type == Type::unreachable) {
					hasUnreachableChild = true;
					break;
				}
			}
			if(hasUnreachableChild) {
				// `curr` is not reachable, so don't emit it.
				return;
			}
			// Control flow requires special handling, but most instructions can be
			// emitted directly after their children.
			if(Properties::isControlFlowStructure(curr)) {
				Visitor<NewBinaryenIRWriter>::visit(curr);
			} else {
				emit(curr);
			}
		}

		template <typename SubType> void NewBinaryenIRWriter<SubType>::visitBlock(Block* curr) {
			auto visitChildren = [this](Block* curr, Index from) {
				auto& list = curr->list;
				while(from < list.size()) {
					auto* child = list[from];
					visit(child);
					if(child->type == Type::unreachable) {
						break;
					}
					++from;
				}
			};

			auto afterChildren = [this](Block* curr) {
				emitScopeEnd(curr);
				if(curr->type == Type::unreachable) {
					// Since this block is unreachable, no instructions will be emitted after
					// it in its enclosing scope. That means that this block will be the last
					// instruction before the end of its parent scope, so its type must match
					// the type of its parent. But we don't have a concrete type for this
					// block and we don't know what type its parent expects, so we can't
					// ensure the types match. To work around this, we insert an `unreachable`
					// instruction after every unreachable control flow structure and depend
					// on its polymorphic behavior to paper over any type mismatches.
					emitUnreachable();
				}
			};

			// Handle very deeply nested blocks in the first position efficiently,
			// avoiding heavy recursion. We only start to do this if we see it will help
			// us (to avoid allocation of the vector).
			if(!curr->list.empty() && curr->list[0]->is<Block>()) {
				std::vector<Block*> parents;
				Block* child;
				while(!curr->list.empty() && (child = curr->list[0]->dynCast<Block>())) {
					parents.push_back(curr);
					emit(curr);
					curr = child;
				}
				// Emit the current block, which does not have a block as a child in the
				// first position.
				emit(curr);
				visitChildren(curr, 0);
				afterChildren(curr);
				bool childUnreachable = curr->type == Type::unreachable;
				// Finish the later parts of all the parent blocks.
				while(!parents.empty()) {
					auto* parent = parents.back();
					parents.pop_back();
					if(!childUnreachable) {
						visitChildren(parent, 1);
					}
					afterChildren(parent);
					childUnreachable = parent->type == Type::unreachable;
				}
				return;
			}
			// Simple case of not having a nested block in the first position.
			emit(curr);
			visitChildren(curr, 0);
			afterChildren(curr);
		}

		template <typename SubType> void NewBinaryenIRWriter<SubType>::visitIf(If* curr) {
			emit(curr);
			visitPossibleBlockContents(curr->ifTrue);

			if(curr->ifFalse) {
				emitIfElse(curr);
				visitPossibleBlockContents(curr->ifFalse);
			}

			emitScopeEnd(curr);
			if(curr->type == Type::unreachable) {
				// We already handled the case of the condition being unreachable in
				// `visit`.  Otherwise, we may still be unreachable, if we are an if-else
				// with both sides unreachable. Just like with blocks, we emit an extra
				// `unreachable` to work around potential type mismatches.
				assert(curr->ifFalse);
				emitUnreachable();
			}
		}

		template <typename SubType> void NewBinaryenIRWriter<SubType>::visitLoop(Loop* curr) {
			emit(curr);
			visitPossibleBlockContents(curr->body);
			emitScopeEnd(curr);
			if(curr->type == Type::unreachable) {
				// we emitted a loop without a return type, so it must not be consumed
				emitUnreachable();
			}
		}

		template <typename SubType> void NewBinaryenIRWriter<SubType>::visitTry(Try* curr) {
			emit(curr);
			visitPossibleBlockContents(curr->body);
			for(Index i = 0; i < curr->catchTags.size(); i++) {
				emitCatch(curr, i);
				visitPossibleBlockContents(curr->catchBodies[i]);
			}
			if(curr->hasCatchAll()) {
				emitCatchAll(curr);
				visitPossibleBlockContents(curr->catchBodies.back());
			}
			if(curr->isDelegate()) {
				emitDelegate(curr);
				// Note that when we emit a delegate we do not need to also emit a scope
				// ending, as the delegate ends the scope.
			} else {
				emitScopeEnd(curr);
			}
			if(curr->type == Type::unreachable) {
				emitUnreachable();
			}
		}

		template <typename SubType> void NewBinaryenIRWriter<SubType>::write() {
			assert(func && "NewBinaryenIRWriter: function is not set");
			emitHeader();
			visitPossibleBlockContents(func->body);
			emitFunctionEnd();
		}

		void NewBinaryInstWriter::emitResultType(Type type) {
			if(type == Type::unreachable) {
				parent.writeType(Type::none);
			} else if(type.isTuple()) {
				writeValue<ValueWritten::Type>(parent.getTypeIndex(Signature(Type::none, type)));
			} else {
				parent.writeType(type);
			}
		}

		void NewBinaryInstWriter::visitBlock(Block* curr) {
			breakStack.push_back(curr->name);
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Block);
			emitResultType(curr->type);
		}

		static Name IMPOSSIBLE_CONTINUE("impossible-continue");
		void NewBinaryInstWriter::visitIf(If* curr) {
			// the binary format requires this; we have a block if we need one
			// TODO: optimize this in Stack IR (if child is a block, we may break to this
			// instead)
			breakStack.emplace_back(IMPOSSIBLE_CONTINUE);
			writeValue<ValueWritten::ASTNode>(BinaryConsts::If);
			emitResultType(curr->type);
		}

		void NewBinaryInstWriter::emitIfElse(If* curr) {
			if(func && !sourceMap) {
				parent.writeExtraDebugLocation(curr, func, BinaryLocations::Else);
			}
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Else);
		}

		void NewBinaryInstWriter::visitLoop(Loop* curr) {
			breakStack.push_back(curr->name);
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Loop);
			emitResultType(curr->type);
		}

		void NewBinaryInstWriter::visitBreak(Break* curr) {
			writeValue<ValueWritten::ASTNode>(curr->condition ? BinaryConsts::BrIf : BinaryConsts::Br);
			writeValue<ValueWritten::BreakIndex>(getBreakIndex(curr->name));
		}

		void NewBinaryInstWriter::visitSwitch(Switch* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::BrTable);
			writeValue<ValueWritten::SwitchTargets>(curr->targets.size());
			for(auto target : curr->targets) {
				writeValue<ValueWritten::BreakIndex>(getBreakIndex(target));
			}
			writeValue<ValueWritten::BreakIndex>(getBreakIndex(curr->default_));
		}

		void NewBinaryInstWriter::visitCall(Call* curr) {
			int8_t op = curr->isReturn ? BinaryConsts::RetCallFunction : BinaryConsts::CallFunction;
			writeValue<ValueWritten::ASTNode>(op);
			writeValue<ValueWritten::FunctionIndex>(parent.getFunctionIndex(curr->target));
		}

		void NewBinaryInstWriter::visitCallIndirect(CallIndirect* curr) {
			Index tableIdx = parent.getTableIndex(curr->table);
			int8_t op      = curr->isReturn ? BinaryConsts::RetCallIndirect : BinaryConsts::CallIndirect;
			writeValue<ValueWritten::ASTNode>(op);
			writeValue<ValueWritten::TypeIndex>(parent.getTypeIndex(curr->heapType));
			writeValue<ValueWritten::TableIndex>(tableIdx);
		}

		void NewBinaryInstWriter::visitLocalGet(LocalGet* curr) {
			size_t numValues = func->getLocalType(curr->index).size();
			for(Index i = 0; i < numValues; ++i) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::LocalGet);
				writeValue<ValueWritten::LocalIndex>(mappedLocals[std::make_pair(curr->index, i)]);
			}
		}

		void NewBinaryInstWriter::visitLocalSet(LocalSet* curr) {
			size_t numValues = func->getLocalType(curr->index).size();
			for(Index i = numValues - 1; i >= 1; --i) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::LocalSet);
				writeValue<ValueWritten::LocalIndex>(mappedLocals[std::make_pair(curr->index, i)]);
			}
			if(!curr->isTee()) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::LocalSet);
				writeValue<ValueWritten::LocalIndex>(mappedLocals[std::make_pair(curr->index, 0)]);
			} else {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::LocalTee);
				writeValue<ValueWritten::LocalIndex>(mappedLocals[std::make_pair(curr->index, 0)]);
				for(Index i = 1; i < numValues; ++i) {
					writeValue<ValueWritten::ASTNode>(BinaryConsts::LocalGet);
					writeValue<ValueWritten::LocalIndex>(mappedLocals[std::make_pair(curr->index, i)]);
				}
			}
		}

		void NewBinaryInstWriter::visitGlobalGet(GlobalGet* curr) {
			// Emit a global.get for each element if this is a tuple global
			Index index      = parent.getGlobalIndex(curr->name);
			size_t numValues = curr->type.size();
			for(Index i = 0; i < numValues; ++i) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GlobalGet);
				writeValue<ValueWritten::GlobalIndex>(index + i);
			}
		}

		void NewBinaryInstWriter::visitGlobalSet(GlobalSet* curr) {
			// Emit a global.set for each element if this is a tuple global
			Index index      = parent.getGlobalIndex(curr->name);
			size_t numValues = parent.getModule()->getGlobal(curr->name)->type.size();
			for(int i = numValues - 1; i >= 0; --i) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GlobalSet);
				writeValue<ValueWritten::GlobalIndex>(index + i);
			}
		}

		void NewBinaryInstWriter::visitLoad(Load* curr) {
			if(!curr->isAtomic) {
				switch(curr->type.getBasic()) {
				case Type::i32: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(
							curr->signed_ ? BinaryConsts::I32LoadMem8S : BinaryConsts::I32LoadMem8U);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(
							curr->signed_ ? BinaryConsts::I32LoadMem16S : BinaryConsts::I32LoadMem16U);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32LoadMem);
						break;
					default:
						abort();
					}
					break;
				}
				case Type::i64: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(
							curr->signed_ ? BinaryConsts::I64LoadMem8S : BinaryConsts::I64LoadMem8U);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(
							curr->signed_ ? BinaryConsts::I64LoadMem16S : BinaryConsts::I64LoadMem16U);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(
							curr->signed_ ? BinaryConsts::I64LoadMem32S : BinaryConsts::I64LoadMem32U);
						break;
					case 8:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64LoadMem);
						break;
					default:
						abort();
					}
					break;
				}
				case Type::f32:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::F32LoadMem);
					break;
				case Type::f64:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::F64LoadMem);
					break;
				case Type::v128:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load);
					break;
				case Type::unreachable:
					// the pointer is unreachable, so we are never reached; just don't emit
					// a load
					return;
				case Type::funcref:
				case Type::anyref:
				case Type::eqref:
				case Type::i31ref:
				case Type::dataref:
				case Type::none:
					WASM_UNREACHABLE("unexpected type");
				}
			} else {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicPrefix);
				switch(curr->type.getBasic()) {
				case Type::i32: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicLoad8U);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicLoad16U);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicLoad);
						break;
					default:
						WASM_UNREACHABLE("invalid load size");
					}
					break;
				}
				case Type::i64: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicLoad8U);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicLoad16U);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicLoad32U);
						break;
					case 8:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicLoad);
						break;
					default:
						WASM_UNREACHABLE("invalid load size");
					}
					break;
				}
				case Type::unreachable:
					return;
				default:
					WASM_UNREACHABLE("unexpected type");
				}
			}
			emitMemoryAccess(curr->align, curr->bytes, curr->offset);
		}

		void NewBinaryInstWriter::visitStore(Store* curr) {
			if(!curr->isAtomic) {
				switch(curr->valueType.getBasic()) {
				case Type::i32: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32StoreMem8);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32StoreMem16);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32StoreMem);
						break;
					default:
						abort();
					}
					break;
				}
				case Type::i64: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64StoreMem8);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64StoreMem16);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64StoreMem32);
						break;
					case 8:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64StoreMem);
						break;
					default:
						abort();
					}
					break;
				}
				case Type::f32:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::F32StoreMem);
					break;
				case Type::f64:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::F64StoreMem);
					break;
				case Type::v128:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Store);
					break;
				case Type::funcref:
				case Type::anyref:
				case Type::eqref:
				case Type::i31ref:
				case Type::dataref:
				case Type::none:
				case Type::unreachable:
					WASM_UNREACHABLE("unexpected type");
				}
			} else {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicPrefix);
				switch(curr->valueType.getBasic()) {
				case Type::i32: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicStore8);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicStore16);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicStore);
						break;
					default:
						WASM_UNREACHABLE("invalid store size");
					}
					break;
				}
				case Type::i64: {
					switch(curr->bytes) {
					case 1:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicStore8);
						break;
					case 2:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicStore16);
						break;
					case 4:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicStore32);
						break;
					case 8:
						writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicStore);
						break;
					default:
						WASM_UNREACHABLE("invalid store size");
					}
					break;
				}
				default:
					WASM_UNREACHABLE("unexpected type");
				}
			}
			emitMemoryAccess(curr->align, curr->bytes, curr->offset);
		}

		void NewBinaryInstWriter::visitAtomicRMW(AtomicRMW* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicPrefix);

#define CASE_FOR_OP(Op)                                                                                                \
	case RMW##Op:                                                                                                      \
		switch(curr->type.getBasic()) {                                                                                \
		case Type::i32:                                                                                                \
			switch(curr->bytes) {                                                                                      \
			case 1:                                                                                                    \
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicRMW##Op##8U);                                 \
				break;                                                                                                 \
			case 2:                                                                                                    \
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicRMW##Op##16U);                                \
				break;                                                                                                 \
			case 4:                                                                                                    \
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicRMW##Op);                                     \
				break;                                                                                                 \
			default:                                                                                                   \
				WASM_UNREACHABLE("invalid rmw size");                                                                  \
			}                                                                                                          \
			break;                                                                                                     \
		case Type::i64:                                                                                                \
			switch(curr->bytes) {                                                                                      \
			case 1:                                                                                                    \
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicRMW##Op##8U);                                 \
				break;                                                                                                 \
			case 2:                                                                                                    \
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicRMW##Op##16U);                                \
				break;                                                                                                 \
			case 4:                                                                                                    \
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicRMW##Op##32U);                                \
				break;                                                                                                 \
			case 8:                                                                                                    \
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicRMW##Op);                                     \
				break;                                                                                                 \
			default:                                                                                                   \
				WASM_UNREACHABLE("invalid rmw size");                                                                  \
			}                                                                                                          \
			break;                                                                                                     \
		default:                                                                                                       \
			WASM_UNREACHABLE("unexpected type");                                                                       \
		}                                                                                                              \
		break

			switch(curr->op) {
				CASE_FOR_OP(Add);
				CASE_FOR_OP(Sub);
				CASE_FOR_OP(And);
				CASE_FOR_OP(Or);
				CASE_FOR_OP(Xor);
				CASE_FOR_OP(Xchg);
			default:
				WASM_UNREACHABLE("unexpected op");
			}
#undef CASE_FOR_OP

			emitMemoryAccess(curr->bytes, curr->bytes, curr->offset);
		}

		void NewBinaryInstWriter::visitAtomicCmpxchg(AtomicCmpxchg* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicPrefix);
			switch(curr->type.getBasic()) {
			case Type::i32:
				switch(curr->bytes) {
				case 1:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicCmpxchg8U);
					break;
				case 2:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicCmpxchg16U);
					break;
				case 4:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicCmpxchg);
					break;
				default:
					WASM_UNREACHABLE("invalid size");
				}
				break;
			case Type::i64:
				switch(curr->bytes) {
				case 1:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicCmpxchg8U);
					break;
				case 2:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicCmpxchg16U);
					break;
				case 4:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicCmpxchg32U);
					break;
				case 8:
					writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicCmpxchg);
					break;
				default:
					WASM_UNREACHABLE("invalid size");
				}
				break;
			default:
				WASM_UNREACHABLE("unexpected type");
			}
			emitMemoryAccess(curr->bytes, curr->bytes, curr->offset);
		}

		void NewBinaryInstWriter::visitAtomicWait(AtomicWait* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicPrefix);
			switch(curr->expectedType.getBasic()) {
			case Type::i32: {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32AtomicWait);
				emitMemoryAccess(4, 4, curr->offset);
				break;
			}
			case Type::i64: {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64AtomicWait);
				emitMemoryAccess(8, 8, curr->offset);
				break;
			}
			default:
				WASM_UNREACHABLE("unexpected type");
			}
		}

		void NewBinaryInstWriter::visitAtomicNotify(AtomicNotify* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicPrefix);
			writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicNotify);
			emitMemoryAccess(4, 4, curr->offset);
		}

		void NewBinaryInstWriter::visitAtomicFence(AtomicFence* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicPrefix);
			writeValue<ValueWritten::ASTNode>(BinaryConsts::AtomicFence);
			writeValue<ValueWritten::AtomicFenceOrder>(curr->order);
		}

		void NewBinaryInstWriter::visitSIMDExtract(SIMDExtract* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
			switch(curr->op) {
			case ExtractLaneSVecI8x16:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16ExtractLaneS);
				break;
			case ExtractLaneUVecI8x16:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16ExtractLaneU);
				break;
			case ExtractLaneSVecI16x8:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtractLaneS);
				break;
			case ExtractLaneUVecI16x8:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtractLaneU);
				break;
			case ExtractLaneVecI32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtractLane);
				break;
			case ExtractLaneVecI64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtractLane);
				break;
			case ExtractLaneVecF32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4ExtractLane);
				break;
			case ExtractLaneVecF64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2ExtractLane);
				break;
			}
			writeValue<ValueWritten::SIMDIndex>(curr->index);
		}

		void NewBinaryInstWriter::visitSIMDReplace(SIMDReplace* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
			switch(curr->op) {
			case ReplaceLaneVecI8x16:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16ReplaceLane);
				break;
			case ReplaceLaneVecI16x8:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ReplaceLane);
				break;
			case ReplaceLaneVecI32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ReplaceLane);
				break;
			case ReplaceLaneVecI64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ReplaceLane);
				break;
			case ReplaceLaneVecF32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4ReplaceLane);
				break;
			case ReplaceLaneVecF64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2ReplaceLane);
				break;
			}
			assert(curr->index < 16);
			writeValue<ValueWritten::SIMDIndex>(curr->index);
		}

		void NewBinaryInstWriter::visitSIMDShuffle(SIMDShuffle* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Shuffle);
			for(uint8_t m : curr->mask) {
				writeValue<ValueWritten::SIMDIndex>(m);
			}
		}

		void NewBinaryInstWriter::visitSIMDTernary(SIMDTernary* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
			switch(curr->op) {
			case Bitselect:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Bitselect);
				break;
			case LaneselectI8x16:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Laneselect);
				break;
			case LaneselectI16x8:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Laneselect);
				break;
			case LaneselectI32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Laneselect);
				break;
			case LaneselectI64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Laneselect);
				break;
			case RelaxedFmaVecF32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4RelaxedFma);
				break;
			case RelaxedFmsVecF32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4RelaxedFms);
				break;
			case RelaxedFmaVecF64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2RelaxedFma);
				break;
			case RelaxedFmsVecF64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2RelaxedFms);
				break;
			case DotI8x16I7x16AddSToVecI32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4DotI8x16I7x16AddS);
				break;
			}
		}

		void NewBinaryInstWriter::visitSIMDShift(SIMDShift* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
			switch(curr->op) {
			case ShlVecI8x16:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Shl);
				break;
			case ShrSVecI8x16:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16ShrS);
				break;
			case ShrUVecI8x16:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16ShrU);
				break;
			case ShlVecI16x8:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Shl);
				break;
			case ShrSVecI16x8:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ShrS);
				break;
			case ShrUVecI16x8:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ShrU);
				break;
			case ShlVecI32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Shl);
				break;
			case ShrSVecI32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ShrS);
				break;
			case ShrUVecI32x4:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ShrU);
				break;
			case ShlVecI64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Shl);
				break;
			case ShrSVecI64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ShrS);
				break;
			case ShrUVecI64x2:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ShrU);
				break;
			}
		}

		void NewBinaryInstWriter::visitSIMDLoad(SIMDLoad* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
			switch(curr->op) {
			case Load8SplatVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load8Splat);
				break;
			case Load16SplatVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load16Splat);
				break;
			case Load32SplatVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load32Splat);
				break;
			case Load64SplatVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load64Splat);
				break;
			case Load8x8SVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load8x8S);
				break;
			case Load8x8UVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load8x8U);
				break;
			case Load16x4SVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load16x4S);
				break;
			case Load16x4UVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load16x4U);
				break;
			case Load32x2SVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load32x2S);
				break;
			case Load32x2UVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load32x2U);
				break;
			case Load32ZeroVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load32Zero);
				break;
			case Load64ZeroVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load64Zero);
				break;
			}
			assert(curr->align);
			emitMemoryAccess(curr->align, /*(unused) bytes=*/0, curr->offset);
		}

		void NewBinaryInstWriter::visitSIMDLoadStoreLane(SIMDLoadStoreLane* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
			switch(curr->op) {
			case Load8LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load8Lane);
				break;
			case Load16LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load16Lane);
				break;
			case Load32LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load32Lane);
				break;
			case Load64LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Load64Lane);
				break;
			case Store8LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Store8Lane);
				break;
			case Store16LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Store16Lane);
				break;
			case Store32LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Store32Lane);
				break;
			case Store64LaneVec128:
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Store64Lane);
				break;
			}
			assert(curr->align);
			emitMemoryAccess(curr->align, /*(unused) bytes=*/0, curr->offset);
			writeValue<ValueWritten::SIMDIndex>(curr->index);
		}

		void NewBinaryInstWriter::visitMemoryInit(MemoryInit* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::MemoryInit);
			writeValue<ValueWritten::MemorySegmentIndex>(curr->segment);
			writeValue<ValueWritten::MemoryIndex>(0);
		}

		void NewBinaryInstWriter::visitDataDrop(DataDrop* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::DataDrop);
			writeValue<ValueWritten::MemorySegmentIndex>(curr->segment);
		}

		void NewBinaryInstWriter::visitMemoryCopy(MemoryCopy* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::MemoryCopy);
			writeValue<ValueWritten::MemoryIndex>(0);
			writeValue<ValueWritten::MemoryIndex>(0);
		}

		void NewBinaryInstWriter::visitMemoryFill(MemoryFill* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::MemoryFill);
			writeValue<ValueWritten::MemoryIndex>(0);
		}

		void NewBinaryInstWriter::visitConst(Const* curr) {
			switch(curr->type.getBasic()) {
			case Type::i32: {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Const);
				writeValue<ValueWritten::ConstS32>(curr->value.geti32());
				break;
			}
			case Type::i64: {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Const);
				writeValue<ValueWritten::ConstS64>(curr->value.geti64());
				break;
			}
			case Type::f32: {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Const);
				writeValue<ValueWritten::ConstF32>(curr->value.reinterpreti32());
				break;
			}
			case Type::f64: {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Const);
				writeValue<ValueWritten::ConstF64>(curr->value.reinterpreti64());
				break;
			}
			case Type::v128: {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Const);
				writeValue<ValueWritten::ConstV128>(curr->value);
				break;
			}
			case Type::funcref:
			case Type::anyref:
			case Type::eqref:
			case Type::i31ref:
			case Type::dataref:
			case Type::none:
			case Type::unreachable:
				WASM_UNREACHABLE("unexpected type");
			}
		}

		void NewBinaryInstWriter::visitUnary(Unary* curr) {
			switch(curr->op) {
			case ClzInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Clz);
				break;
			case CtzInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Ctz);
				break;
			case PopcntInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Popcnt);
				break;
			case EqZInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32EqZ);
				break;
			case ClzInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Clz);
				break;
			case CtzInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Ctz);
				break;
			case PopcntInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Popcnt);
				break;
			case EqZInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64EqZ);
				break;
			case NegFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Neg);
				break;
			case AbsFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Abs);
				break;
			case CeilFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Ceil);
				break;
			case FloorFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Floor);
				break;
			case TruncFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Trunc);
				break;
			case NearestFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32NearestInt);
				break;
			case SqrtFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Sqrt);
				break;
			case NegFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Neg);
				break;
			case AbsFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Abs);
				break;
			case CeilFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Ceil);
				break;
			case FloorFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Floor);
				break;
			case TruncFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Trunc);
				break;
			case NearestFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64NearestInt);
				break;
			case SqrtFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Sqrt);
				break;
			case ExtendSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64SExtendI32);
				break;
			case ExtendUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64UExtendI32);
				break;
			case WrapInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32WrapI64);
				break;
			case TruncUFloat32ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32UTruncF32);
				break;
			case TruncUFloat32ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64UTruncF32);
				break;
			case TruncSFloat32ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32STruncF32);
				break;
			case TruncSFloat32ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64STruncF32);
				break;
			case TruncUFloat64ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32UTruncF64);
				break;
			case TruncUFloat64ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64UTruncF64);
				break;
			case TruncSFloat64ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32STruncF64);
				break;
			case TruncSFloat64ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64STruncF64);
				break;
			case ConvertUInt32ToFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32UConvertI32);
				break;
			case ConvertUInt32ToFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64UConvertI32);
				break;
			case ConvertSInt32ToFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32SConvertI32);
				break;
			case ConvertSInt32ToFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64SConvertI32);
				break;
			case ConvertUInt64ToFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32UConvertI64);
				break;
			case ConvertUInt64ToFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64UConvertI64);
				break;
			case ConvertSInt64ToFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32SConvertI64);
				break;
			case ConvertSInt64ToFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64SConvertI64);
				break;
			case DemoteFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32DemoteI64);
				break;
			case PromoteFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64PromoteF32);
				break;
			case ReinterpretFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32ReinterpretF32);
				break;
			case ReinterpretFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64ReinterpretF64);
				break;
			case ReinterpretInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32ReinterpretI32);
				break;
			case ReinterpretInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64ReinterpretI64);
				break;
			case ExtendS8Int32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32ExtendS8);
				break;
			case ExtendS16Int32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32ExtendS16);
				break;
			case ExtendS8Int64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64ExtendS8);
				break;
			case ExtendS16Int64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64ExtendS16);
				break;
			case ExtendS32Int64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64ExtendS32);
				break;
			case TruncSatSFloat32ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32STruncSatF32);
				break;
			case TruncSatUFloat32ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32UTruncSatF32);
				break;
			case TruncSatSFloat64ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32STruncSatF64);
				break;
			case TruncSatUFloat64ToInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32UTruncSatF64);
				break;
			case TruncSatSFloat32ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64STruncSatF32);
				break;
			case TruncSatUFloat32ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64UTruncSatF32);
				break;
			case TruncSatSFloat64ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64STruncSatF64);
				break;
			case TruncSatUFloat64ToInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64UTruncSatF64);
				break;
			case SplatVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Splat);
				break;
			case SplatVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Splat);
				break;
			case SplatVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Splat);
				break;
			case SplatVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Splat);
				break;
			case SplatVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Splat);
				break;
			case SplatVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Splat);
				break;
			case NotVec128:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Not);
				break;
			case AnyTrueVec128:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128AnyTrue);
				break;
			case AbsVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Abs);
				break;
			case NegVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Neg);
				break;
			case AllTrueVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16AllTrue);
				break;
			case BitmaskVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Bitmask);
				break;
			case PopcntVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Popcnt);
				break;
			case AbsVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Abs);
				break;
			case NegVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Neg);
				break;
			case AllTrueVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8AllTrue);
				break;
			case BitmaskVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Bitmask);
				break;
			case AbsVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Abs);
				break;
			case NegVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Neg);
				break;
			case AllTrueVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4AllTrue);
				break;
			case BitmaskVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Bitmask);
				break;
			case AbsVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Abs);
				break;
			case NegVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Neg);
				break;
			case AllTrueVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2AllTrue);
				break;
			case BitmaskVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Bitmask);
				break;
			case AbsVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Abs);
				break;
			case NegVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Neg);
				break;
			case SqrtVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Sqrt);
				break;
			case CeilVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Ceil);
				break;
			case FloorVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Floor);
				break;
			case TruncVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Trunc);
				break;
			case NearestVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Nearest);
				break;
			case AbsVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Abs);
				break;
			case NegVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Neg);
				break;
			case SqrtVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Sqrt);
				break;
			case CeilVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Ceil);
				break;
			case FloorVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Floor);
				break;
			case TruncVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Trunc);
				break;
			case NearestVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Nearest);
				break;
			case ExtAddPairwiseSVecI8x16ToI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtaddPairwiseI8x16S);
				break;
			case ExtAddPairwiseUVecI8x16ToI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtaddPairwiseI8x16U);
				break;
			case ExtAddPairwiseSVecI16x8ToI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtaddPairwiseI16x8S);
				break;
			case ExtAddPairwiseUVecI16x8ToI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtaddPairwiseI16x8U);
				break;
			case TruncSatSVecF32x4ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4TruncSatF32x4S);
				break;
			case TruncSatUVecF32x4ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4TruncSatF32x4U);
				break;
			case ConvertSVecI32x4ToVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4ConvertI32x4S);
				break;
			case ConvertUVecI32x4ToVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4ConvertI32x4U);
				break;
			case ExtendLowSVecI8x16ToVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtendLowI8x16S);
				break;
			case ExtendHighSVecI8x16ToVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtendHighI8x16S);
				break;
			case ExtendLowUVecI8x16ToVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtendLowI8x16U);
				break;
			case ExtendHighUVecI8x16ToVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtendHighI8x16U);
				break;
			case ExtendLowSVecI16x8ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtendLowI16x8S);
				break;
			case ExtendHighSVecI16x8ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtendHighI16x8S);
				break;
			case ExtendLowUVecI16x8ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtendLowI16x8U);
				break;
			case ExtendHighUVecI16x8ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtendHighI16x8U);
				break;
			case ExtendLowSVecI32x4ToVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtendLowI32x4S);
				break;
			case ExtendHighSVecI32x4ToVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtendHighI32x4S);
				break;
			case ExtendLowUVecI32x4ToVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtendLowI32x4U);
				break;
			case ExtendHighUVecI32x4ToVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtendHighI32x4U);
				break;
			case ConvertLowSVecI32x4ToVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2ConvertLowI32x4S);
				break;
			case ConvertLowUVecI32x4ToVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2ConvertLowI32x4U);
				break;
			case TruncSatZeroSVecF64x2ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4TruncSatF64x2SZero);
				break;
			case TruncSatZeroUVecF64x2ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4TruncSatF64x2UZero);
				break;
			case DemoteZeroVecF64x2ToVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4DemoteF64x2Zero);
				break;
			case PromoteLowVecF32x4ToVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2PromoteLowF32x4);
				break;
			case RelaxedTruncSVecF32x4ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4RelaxedTruncF32x4S);
				break;
			case RelaxedTruncUVecF32x4ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4RelaxedTruncF32x4U);
				break;
			case RelaxedTruncZeroSVecF64x2ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4RelaxedTruncF64x2SZero);
				break;
			case RelaxedTruncZeroUVecF64x2ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4RelaxedTruncF64x2UZero);
				break;
			case InvalidUnary:
				WASM_UNREACHABLE("invalid unary op");
			}
		}

		void NewBinaryInstWriter::visitBinary(Binary* curr) {
			switch(curr->op) {
			case AddInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Add);
				break;
			case SubInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Sub);
				break;
			case MulInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Mul);
				break;
			case DivSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32DivS);
				break;
			case DivUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32DivU);
				break;
			case RemSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32RemS);
				break;
			case RemUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32RemU);
				break;
			case AndInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32And);
				break;
			case OrInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Or);
				break;
			case XorInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Xor);
				break;
			case ShlInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Shl);
				break;
			case ShrUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32ShrU);
				break;
			case ShrSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32ShrS);
				break;
			case RotLInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32RotL);
				break;
			case RotRInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32RotR);
				break;
			case EqInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Eq);
				break;
			case NeInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32Ne);
				break;
			case LtSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32LtS);
				break;
			case LtUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32LtU);
				break;
			case LeSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32LeS);
				break;
			case LeUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32LeU);
				break;
			case GtSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32GtS);
				break;
			case GtUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32GtU);
				break;
			case GeSInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32GeS);
				break;
			case GeUInt32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I32GeU);
				break;

			case AddInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Add);
				break;
			case SubInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Sub);
				break;
			case MulInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Mul);
				break;
			case DivSInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64DivS);
				break;
			case DivUInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64DivU);
				break;
			case RemSInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64RemS);
				break;
			case RemUInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64RemU);
				break;
			case AndInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64And);
				break;
			case OrInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Or);
				break;
			case XorInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Xor);
				break;
			case ShlInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Shl);
				break;
			case ShrUInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64ShrU);
				break;
			case ShrSInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64ShrS);
				break;
			case RotLInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64RotL);
				break;
			case RotRInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64RotR);
				break;
			case EqInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Eq);
				break;
			case NeInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64Ne);
				break;
			case LtSInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64LtS);
				break;
			case LtUInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64LtU);
				break;
			case LeSInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64LeS);
				break;
			case LeUInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64LeU);
				break;
			case GtSInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64GtS);
				break;
			case GtUInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64GtU);
				break;
			case GeSInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64GeS);
				break;
			case GeUInt64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::I64GeU);
				break;

			case AddFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Add);
				break;
			case SubFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Sub);
				break;
			case MulFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Mul);
				break;
			case DivFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Div);
				break;
			case CopySignFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32CopySign);
				break;
			case MinFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Min);
				break;
			case MaxFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Max);
				break;
			case EqFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Eq);
				break;
			case NeFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Ne);
				break;
			case LtFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Lt);
				break;
			case LeFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Le);
				break;
			case GtFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Gt);
				break;
			case GeFloat32:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F32Ge);
				break;

			case AddFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Add);
				break;
			case SubFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Sub);
				break;
			case MulFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Mul);
				break;
			case DivFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Div);
				break;
			case CopySignFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64CopySign);
				break;
			case MinFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Min);
				break;
			case MaxFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Max);
				break;
			case EqFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Eq);
				break;
			case NeFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Ne);
				break;
			case LtFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Lt);
				break;
			case LeFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Le);
				break;
			case GtFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Gt);
				break;
			case GeFloat64:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::F64Ge);
				break;

			case EqVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Eq);
				break;
			case NeVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Ne);
				break;
			case LtSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16LtS);
				break;
			case LtUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16LtU);
				break;
			case GtSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16GtS);
				break;
			case GtUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16GtU);
				break;
			case LeSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16LeS);
				break;
			case LeUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16LeU);
				break;
			case GeSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16GeS);
				break;
			case GeUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16GeU);
				break;
			case EqVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Eq);
				break;
			case NeVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Ne);
				break;
			case LtSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8LtS);
				break;
			case LtUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8LtU);
				break;
			case GtSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8GtS);
				break;
			case GtUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8GtU);
				break;
			case LeSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8LeS);
				break;
			case LeUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8LeU);
				break;
			case GeSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8GeS);
				break;
			case GeUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8GeU);
				break;
			case EqVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Eq);
				break;
			case NeVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Ne);
				break;
			case LtSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4LtS);
				break;
			case LtUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4LtU);
				break;
			case GtSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4GtS);
				break;
			case GtUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4GtU);
				break;
			case LeSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4LeS);
				break;
			case LeUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4LeU);
				break;
			case GeSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4GeS);
				break;
			case GeUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4GeU);
				break;
			case EqVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Eq);
				break;
			case NeVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Ne);
				break;
			case LtSVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2LtS);
				break;
			case GtSVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2GtS);
				break;
			case LeSVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2LeS);
				break;
			case GeSVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2GeS);
				break;
			case EqVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Eq);
				break;
			case NeVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Ne);
				break;
			case LtVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Lt);
				break;
			case GtVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Gt);
				break;
			case LeVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Le);
				break;
			case GeVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Ge);
				break;
			case EqVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Eq);
				break;
			case NeVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Ne);
				break;
			case LtVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Lt);
				break;
			case GtVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Gt);
				break;
			case LeVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Le);
				break;
			case GeVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Ge);
				break;
			case AndVec128:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128And);
				break;
			case OrVec128:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Or);
				break;
			case XorVec128:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Xor);
				break;
			case AndNotVec128:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::V128Andnot);
				break;
			case AddVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Add);
				break;
			case AddSatSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16AddSatS);
				break;
			case AddSatUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16AddSatU);
				break;
			case SubVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Sub);
				break;
			case SubSatSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16SubSatS);
				break;
			case SubSatUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16SubSatU);
				break;
			case MinSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16MinS);
				break;
			case MinUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16MinU);
				break;
			case MaxSVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16MaxS);
				break;
			case MaxUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16MaxU);
				break;
			case AvgrUVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16AvgrU);
				break;
			case AddVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Add);
				break;
			case AddSatSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8AddSatS);
				break;
			case AddSatUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8AddSatU);
				break;
			case SubVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Sub);
				break;
			case SubSatSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8SubSatS);
				break;
			case SubSatUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8SubSatU);
				break;
			case MulVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Mul);
				break;
			case MinSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8MinS);
				break;
			case MinUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8MinU);
				break;
			case MaxSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8MaxS);
				break;
			case MaxUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8MaxU);
				break;
			case AvgrUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8AvgrU);
				break;
			case Q15MulrSatSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8Q15MulrSatS);
				break;
			case ExtMulLowSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtmulLowI8x16S);
				break;
			case ExtMulHighSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtmulHighI8x16S);
				break;
			case ExtMulLowUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtmulLowI8x16U);
				break;
			case ExtMulHighUVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8ExtmulHighI8x16U);
				break;
			case AddVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Add);
				break;
			case SubVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Sub);
				break;
			case MulVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4Mul);
				break;
			case MinSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4MinS);
				break;
			case MinUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4MinU);
				break;
			case MaxSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4MaxS);
				break;
			case MaxUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4MaxU);
				break;
			case DotSVecI16x8ToVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4DotI16x8S);
				break;
			case ExtMulLowSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtmulLowI16x8S);
				break;
			case ExtMulHighSVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtmulHighI16x8S);
				break;
			case ExtMulLowUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtmulLowI16x8U);
				break;
			case ExtMulHighUVecI32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I32x4ExtmulHighI16x8U);
				break;
			case AddVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Add);
				break;
			case SubVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Sub);
				break;
			case MulVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2Mul);
				break;
			case ExtMulLowSVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtmulLowI32x4S);
				break;
			case ExtMulHighSVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtmulHighI32x4S);
				break;
			case ExtMulLowUVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtmulLowI32x4U);
				break;
			case ExtMulHighUVecI64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I64x2ExtmulHighI32x4U);
				break;

			case AddVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Add);
				break;
			case SubVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Sub);
				break;
			case MulVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Mul);
				break;
			case DivVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Div);
				break;
			case MinVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Min);
				break;
			case MaxVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Max);
				break;
			case PMinVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Pmin);
				break;
			case PMaxVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4Pmax);
				break;
			case AddVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Add);
				break;
			case SubVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Sub);
				break;
			case MulVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Mul);
				break;
			case DivVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Div);
				break;
			case MinVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Min);
				break;
			case MaxVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Max);
				break;
			case PMinVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Pmin);
				break;
			case PMaxVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2Pmax);
				break;

			case NarrowSVecI16x8ToVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16NarrowI16x8S);
				break;
			case NarrowUVecI16x8ToVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16NarrowI16x8U);
				break;
			case NarrowSVecI32x4ToVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8NarrowI32x4S);
				break;
			case NarrowUVecI32x4ToVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8NarrowI32x4U);
				break;

			case SwizzleVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16Swizzle);
				break;

			case RelaxedSwizzleVecI8x16:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I8x16RelaxedSwizzle);
				break;
			case RelaxedMinVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4RelaxedMin);
				break;
			case RelaxedMaxVecF32x4:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F32x4RelaxedMax);
				break;
			case RelaxedMinVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2RelaxedMin);
				break;
			case RelaxedMaxVecF64x2:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::F64x2RelaxedMax);
				break;
			case RelaxedQ15MulrSVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8RelaxedQ15MulrS);
				break;
			case DotI8x16I7x16SToVecI16x8:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SIMDPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::I16x8DotI8x16I7x16S);
				break;

			case InvalidBinary:
				WASM_UNREACHABLE("invalid binary op");
			}
		}

		void NewBinaryInstWriter::visitSelect(Select* curr) {
			if(curr->type.isRef()) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::SelectWithType);
				writeValue<ValueWritten::NumSelectTypes>(curr->type.size());
				for(size_t i = 0; i < curr->type.size(); i++) {
					parent.writeType(curr->type != Type::unreachable ? curr->type : Type::none);
				}
			} else {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::Select);
			}
		}

		void NewBinaryInstWriter::visitReturn(Return* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Return);
		}

		void NewBinaryInstWriter::visitMemorySize(MemorySize* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MemorySize);
			writeValue<ValueWritten::MemorySizeFlags>(0);
		}

		void NewBinaryInstWriter::visitMemoryGrow(MemoryGrow* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MemoryGrow);
			// Reserved flags field
			writeValue<ValueWritten::MemoryGrowFlags>(0);
		}

		void NewBinaryInstWriter::visitRefNull(RefNull* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::RefNull);
			parent.writeHeapType(curr->type.getHeapType());
		}

		void NewBinaryInstWriter::visitRefIs(RefIs* curr) {
			switch(curr->op) {
			case RefIsNull:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::RefIsNull);
				break;
			case RefIsFunc:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode>(BinaryConsts::RefIsFunc);
				break;
			case RefIsData:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode>(BinaryConsts::RefIsData);
				break;
			case RefIsI31:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode>(BinaryConsts::RefIsI31);
				break;
			default:
				WASM_UNREACHABLE("unimplemented ref.is_*");
			}
		}

		void NewBinaryInstWriter::visitRefFunc(RefFunc* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::RefFunc);
			writeValue<ValueWritten::FunctionIndex>(parent.getFunctionIndex(curr->func));
		}

		void NewBinaryInstWriter::visitRefEq(RefEq* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::RefEq);
		}

		void NewBinaryInstWriter::visitTableGet(TableGet* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::TableGet);
			writeValue<ValueWritten::TableIndex>(parent.getTableIndex(curr->table));
		}

		void NewBinaryInstWriter::visitTableSet(TableSet* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::TableSet);
			writeValue<ValueWritten::TableIndex>(parent.getTableIndex(curr->table));
		}

		void NewBinaryInstWriter::visitTableSize(TableSize* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::TableSize);
			writeValue<ValueWritten::TableIndex>(parent.getTableIndex(curr->table));
		}

		void NewBinaryInstWriter::visitTableGrow(TableGrow* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::MiscPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::TableGrow);
			writeValue<ValueWritten::TableIndex>(parent.getTableIndex(curr->table));
		}

		void NewBinaryInstWriter::visitTry(Try* curr) {
			breakStack.push_back(curr->name);
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Try);
			emitResultType(curr->type);
		}

		void NewBinaryInstWriter::emitCatch(Try* curr, Index i) {
			if(func && !sourceMap) {
				parent.writeExtraDebugLocation(curr, func, i);
			}
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Catch);
			writeValue<ValueWritten::TagIndex>(parent.getTagIndex(curr->catchTags[i]));
		}

		void NewBinaryInstWriter::emitCatchAll(Try* curr) {
			if(func && !sourceMap) {
				parent.writeExtraDebugLocation(curr, func, curr->catchBodies.size());
			}
			writeValue<ValueWritten::ASTNode>(BinaryConsts::CatchAll);
		}

		void NewBinaryInstWriter::emitDelegate(Try* curr) {
			// The delegate ends the scope in effect, and pops the try's name. Note that
			// the getBreakIndex is intentionally after that pop, as the delegate cannot
			// target its own try.
			assert(!breakStack.empty());
			breakStack.pop_back();
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Delegate);
			writeValue<ValueWritten::BreakIndex>(getBreakIndex(curr->delegateTarget));
		}

		void NewBinaryInstWriter::visitThrow(Throw* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Throw);
			writeValue<ValueWritten::TagIndex>(parent.getTagIndex(curr->tag));
		}

		void NewBinaryInstWriter::visitRethrow(Rethrow* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Rethrow);
			writeValue<ValueWritten::BreakIndex>(getBreakIndex(curr->target));
		}

		void NewBinaryInstWriter::visitNop(Nop* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Nop);
		}

		void NewBinaryInstWriter::visitUnreachable(Unreachable* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Unreachable);
		}

		void NewBinaryInstWriter::visitDrop(Drop* curr) {
			size_t numValues = curr->value->type.size();
			for(size_t i = 0; i < numValues; i++) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::Drop);
			}
		}

		void NewBinaryInstWriter::visitPop(Pop* curr) {
			// Turns into nothing in the binary format
		}

		void NewBinaryInstWriter::visitTupleMake(TupleMake* curr) {
			// Turns into nothing in the binary format
		}

		void NewBinaryInstWriter::visitTupleExtract(TupleExtract* curr) {
			size_t numVals = curr->tuple->type.size();
			// Drop all values after the one we want
			for(size_t i = curr->index + 1; i < numVals; ++i) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::Drop);
			}
			// If the extracted value is the only one left, we're done
			if(curr->index == 0) {
				return;
			}
			// Otherwise, save it to a scratch local, drop the others, then retrieve it
			assert(scratchLocals.find(curr->type) != scratchLocals.end());
			auto scratch = scratchLocals[curr->type];
			writeValue<ValueWritten::ASTNode>(BinaryConsts::LocalSet);
			writeValue<ValueWritten::ScratchLocalIndex>(scratch);
			for(size_t i = 0; i < curr->index; ++i) {
				writeValue<ValueWritten::ASTNode>(BinaryConsts::Drop);
			}
			writeValue<ValueWritten::ASTNode>(BinaryConsts::LocalGet);
			writeValue<ValueWritten::ScratchLocalIndex>(scratch);
		}

		void NewBinaryInstWriter::visitI31New(I31New* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::I31New);
		}

		void NewBinaryInstWriter::visitI31Get(I31Get* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(curr->signed_ ? BinaryConsts::I31GetS : BinaryConsts::I31GetU);
		}

		void NewBinaryInstWriter::visitCallRef(CallRef* curr) {
			writeValue<ValueWritten::ASTNode>(curr->isReturn ? BinaryConsts::RetCallRef : BinaryConsts::CallRef);
		}

		void NewBinaryInstWriter::visitRefTest(RefTest* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			if(curr->rtt) {
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefTest);
			} else {
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefTestStatic);
				parent.writeIndexedHeapType(curr->intendedType);
			}
		}

		void NewBinaryInstWriter::visitRefCast(RefCast* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			if(curr->rtt) {
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefCast);
			} else {
				if(curr->safety == RefCast::Unsafe) {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefCastNopStatic);
				} else {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefCastStatic);
				}
				parent.writeIndexedHeapType(curr->intendedType);
			}
		}

		void NewBinaryInstWriter::visitBrOn(BrOn* curr) {
			switch(curr->op) {
			case BrOnNull:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::BrOnNull);
				break;
			case BrOnNonNull:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::BrOnNonNull);
				break;
			case BrOnCast:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				if(curr->rtt) {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnCast);
				} else {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnCastStatic);
				}
				break;
			case BrOnCastFail:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				if(curr->rtt) {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnCastFail);
				} else {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnCastStaticFail);
				}
				break;
			case BrOnFunc:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnFunc);
				break;
			case BrOnNonFunc:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnNonFunc);
				break;
			case BrOnData:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnData);
				break;
			case BrOnNonData:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnNonData);
				break;
			case BrOnI31:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnI31);
				break;
			case BrOnNonI31:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::BrOnNonI31);
				break;
			default:
				WASM_UNREACHABLE("invalid br_on_*");
			}
			writeValue<ValueWritten::BreakIndex>(getBreakIndex(curr->name));
			if((curr->op == BrOnCast || curr->op == BrOnCastFail) && !curr->rtt) {
				parent.writeIndexedHeapType(curr->intendedType);
			}
		}

		void NewBinaryInstWriter::visitRttCanon(RttCanon* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::RttCanon);
			parent.writeIndexedHeapType(curr->type.getRtt().heapType);
		}

		void NewBinaryInstWriter::visitRttSub(RttSub* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(curr->fresh ? BinaryConsts::RttFreshSub : BinaryConsts::RttSub);
			parent.writeIndexedHeapType(curr->type.getRtt().heapType);
		}

		void NewBinaryInstWriter::visitStructNew(StructNew* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			if(curr->rtt) {
				if(curr->isWithDefault()) {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::StructNewDefaultWithRtt);
				} else {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::StructNewWithRtt);
				}
			} else {
				if(curr->isWithDefault()) {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::StructNewDefault);
				} else {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::StructNew);
				}
			}
			parent.writeIndexedHeapType(curr->type.getHeapType());
		}

		void NewBinaryInstWriter::visitStructGet(StructGet* curr) {
			const auto& heapType = curr->ref->type.getHeapType();
			const auto& field    = heapType.getStruct().fields[curr->index];
			int8_t op;
			if(field.type != Type::i32 || field.packedType == Field::not_packed) {
				op = BinaryConsts::StructGet;
			} else if(curr->signed_) {
				op = BinaryConsts::StructGetS;
			} else {
				op = BinaryConsts::StructGetU;
			}
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(op);
			parent.writeIndexedHeapType(heapType);
			writeValue<ValueWritten::StructFieldIndex>(curr->index);
		}

		void NewBinaryInstWriter::visitStructSet(StructSet* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::StructSet);
			parent.writeIndexedHeapType(curr->ref->type.getHeapType());
			writeValue<ValueWritten::StructFieldIndex>(curr->index);
		}

		void NewBinaryInstWriter::visitArrayNew(ArrayNew* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			if(curr->rtt) {
				if(curr->isWithDefault()) {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayNewDefaultWithRtt);
				} else {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayNewWithRtt);
				}
			} else {
				if(curr->isWithDefault()) {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayNewDefault);
				} else {
					writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayNew);
				}
			}
			parent.writeIndexedHeapType(curr->type.getHeapType());
		}

		void NewBinaryInstWriter::visitArrayInit(ArrayInit* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			if(curr->rtt) {
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayInit);
			} else {
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayInitStatic);
			}
			parent.writeIndexedHeapType(curr->type.getHeapType());
			writeValue<ValueWritten::ArraySize>(curr->values.size());
		}

		void NewBinaryInstWriter::visitArrayGet(ArrayGet* curr) {
			auto heapType     = curr->ref->type.getHeapType();
			const auto& field = heapType.getArray().element;
			int8_t op;
			if(field.type != Type::i32 || field.packedType == Field::not_packed) {
				op = BinaryConsts::ArrayGet;
			} else if(curr->signed_) {
				op = BinaryConsts::ArrayGetS;
			} else {
				op = BinaryConsts::ArrayGetU;
			}
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(op);
			parent.writeIndexedHeapType(heapType);
		}

		void NewBinaryInstWriter::visitArraySet(ArraySet* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArraySet);
			parent.writeIndexedHeapType(curr->ref->type.getHeapType());
		}

		void NewBinaryInstWriter::visitArrayLen(ArrayLen* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayLen);
			parent.writeIndexedHeapType(curr->ref->type.getHeapType());
		}

		void NewBinaryInstWriter::visitArrayCopy(ArrayCopy* curr) {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
			writeValue<ValueWritten::ASTNode32>(BinaryConsts::ArrayCopy);
			parent.writeIndexedHeapType(curr->destRef->type.getHeapType());
			parent.writeIndexedHeapType(curr->srcRef->type.getHeapType());
		}

		void NewBinaryInstWriter::visitRefAs(RefAs* curr) {
			switch(curr->op) {
			case RefAsNonNull:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::RefAsNonNull);
				break;
			case RefAsFunc:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefAsFunc);
				break;
			case RefAsData:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefAsData);
				break;
			case RefAsI31:
				writeValue<ValueWritten::ASTNode>(BinaryConsts::GCPrefix);
				writeValue<ValueWritten::ASTNode32>(BinaryConsts::RefAsI31);
				break;
			default:
				WASM_UNREACHABLE("invalid ref.as_*");
			}
		}

		void NewBinaryInstWriter::emitScopeEnd(Expression* curr) {
			assert(!breakStack.empty());
			breakStack.pop_back();
			writeValue<ValueWritten::ASTNode>(BinaryConsts::End);
			if(func && !sourceMap) {
				parent.writeDebugLocationEnd(curr, func);
			}
		}

		void NewBinaryInstWriter::emitFunctionEnd() {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::End);
		}

		void NewBinaryInstWriter::emitUnreachable() {
			writeValue<ValueWritten::ASTNode>(BinaryConsts::Unreachable);
		}

		void NewBinaryInstWriter::mapLocalsAndEmitHeader() {
			assert(func && "NewBinaryInstWriter: function is not set");
			// Map params
			for(Index i = 0; i < func->getNumParams(); i++) {
				mappedLocals[std::make_pair(i, 0)] = i;
			}
			// Normally we map all locals of the same type into a range of adjacent
			// addresses, which is more compact. However, if we need to keep DWARF valid,
			// do not do any reordering at all - instead, do a trivial mapping that
			// keeps everything unmoved.
			if(DWARF) {
				FindAll<TupleExtract> extracts(func->body);
				if(!extracts.list.empty()) {
					Fatal() << "DWARF + multivalue is not yet complete";
				}
				Index varStart = func->getVarIndexBase();
				Index varEnd   = varStart + func->getNumVars();
				writeValue<ValueWritten::NumFunctionLocals>(func->getNumVars());
				for(Index i = varStart; i < varEnd; i++) {
					mappedLocals[std::make_pair(i, 0)] = i;
					writeValue<ValueWritten::FunctionLocalSize>(1);
					parent.writeType(func->getLocalType(i));
				}
				return;
			}
			for(auto type : func->vars) {
				for(const auto& t : type) {
					noteLocalType(t);
				}
			}
			countScratchLocals();
			std::unordered_map<Type, size_t> currLocalsByType;
			for(Index i = func->getVarIndexBase(); i < func->getNumLocals(); i++) {
				Index j = 0;
				for(const auto& type : func->getLocalType(i)) {
					auto fullIndex = std::make_pair(i, j++);
					Index index    = func->getVarIndexBase();
					for(auto& localType : localTypes) {
						if(type == localType) {
							mappedLocals[fullIndex] = index + currLocalsByType[localType];
							currLocalsByType[type]++;
							break;
						}
						index += numLocalsByType.at(localType);
					}
				}
			}
			setScratchLocals();
			writeValue<ValueWritten::CountNumLocalsByType>(numLocalsByType.size());
			for(auto& localType : localTypes) {
				writeValue<ValueWritten::NumLocalsByType>(numLocalsByType.at(localType));
				parent.writeType(localType);
			}
		}

		void NewBinaryInstWriter::noteLocalType(Type type) {
			if(!numLocalsByType.count(type)) {
				localTypes.push_back(type);
			}
			numLocalsByType[type]++;
		}

		void NewBinaryInstWriter::countScratchLocals() {
			// Add a scratch register in `numLocalsByType` for each type of
			// tuple.extract with nonzero index present.
			FindAll<TupleExtract> extracts(func->body);
			for(auto* extract : extracts.list) {
				if(extract->type != Type::unreachable && extract->index != 0) {
					scratchLocals[extract->type] = 0;
				}
			}
			for(auto& [type, _] : scratchLocals) {
				noteLocalType(type);
			}
		}

		void NewBinaryInstWriter::setScratchLocals() {
			Index index = func->getVarIndexBase();
			for(auto& localType : localTypes) {
				index += numLocalsByType[localType];
				if(scratchLocals.find(localType) != scratchLocals.end()) {
					scratchLocals[localType] = index - 1;
				}
			}
		}

		void NewBinaryInstWriter::emitMemoryAccess(size_t alignment, size_t bytes, uint32_t offset) {
			writeValue<ValueWritten::MemoryAccessAlignment>(Bits::log2(alignment ? alignment : bytes));
			writeValue<ValueWritten::MemoryAccessOffset>(offset);
		}

		int32_t NewBinaryInstWriter::getBreakIndex(Name name) { // -1 if not found
			if(name == DELEGATE_CALLER_TARGET) {
				return breakStack.size();
			}
			for(int i = breakStack.size() - 1; i >= 0; i--) {
				if(breakStack[i] == name) {
					return breakStack.size() - 1 - i;
				}
			}
			WASM_UNREACHABLE("break index not found");
		}

		void NewStackIRGenerator::emit(Expression* curr) {
			StackInst* stackInst = nullptr;
			if(curr->is<Block>()) {
				stackInst = makeStackInst(StackInst::BlockBegin, curr);
			} else if(curr->is<If>()) {
				stackInst = makeStackInst(StackInst::IfBegin, curr);
			} else if(curr->is<Loop>()) {
				stackInst = makeStackInst(StackInst::LoopBegin, curr);
			} else if(curr->is<Try>()) {
				stackInst = makeStackInst(StackInst::TryBegin, curr);
			} else {
				stackInst = makeStackInst(curr);
			}
			stackIR.push_back(stackInst);
		}

		void NewStackIRGenerator::emitScopeEnd(Expression* curr) {
			StackInst* stackInst = nullptr;
			if(curr->is<Block>()) {
				stackInst = makeStackInst(StackInst::BlockEnd, curr);
			} else if(curr->is<If>()) {
				stackInst = makeStackInst(StackInst::IfEnd, curr);
			} else if(curr->is<Loop>()) {
				stackInst = makeStackInst(StackInst::LoopEnd, curr);
			} else if(curr->is<Try>()) {
				stackInst = makeStackInst(StackInst::TryEnd, curr);
			} else {
				WASM_UNREACHABLE("unexpected expr type");
			}
			stackIR.push_back(stackInst);
		}

		StackInst* NewStackIRGenerator::makeStackInst(StackInst::Op op, Expression* origin) {
			auto* ret      = module.allocator.alloc<StackInst>();
			ret->op        = op;
			ret->origin    = origin;
			auto stackType = origin->type;
			if(origin->is<Block>() || origin->is<Loop>() || origin->is<If>() || origin->is<Try>()) {
				if(stackType == Type::unreachable) {
					// There are no unreachable blocks, loops, or ifs. we emit extra
					// unreachables to fix that up, so that they are valid as having none
					// type.
					stackType = Type::none;
				} else if(op != StackInst::BlockEnd && op != StackInst::IfEnd && op != StackInst::LoopEnd
						  && op != StackInst::TryEnd) {
					// If a concrete type is returned, we mark the end of the construct has
					// having that type (as it is pushed to the value stack at that point),
					// other parts are marked as none).
					stackType = Type::none;
				}
			}
			ret->type = stackType;
			return ret;
		}

		void NewStackIRToBinaryWriter::write() {
			writer.mapLocalsAndEmitHeader();
			// Stack to track indices of catches within a try
			SmallVector<Index, 4> catchIndexStack;
			for(auto* inst : *func->stackIR) {
				if(!inst) {
					continue; // a nullptr is just something we can skip
				}
				switch(inst->op) {
				case StackInst::TryBegin:
					catchIndexStack.push_back(0);
					[[fallthrough]];
				case StackInst::Basic:
				case StackInst::BlockBegin:
				case StackInst::IfBegin:
				case StackInst::LoopBegin: {
					writer.visit(inst->origin);
					break;
				}
				case StackInst::TryEnd:
					catchIndexStack.pop_back();
					[[fallthrough]];
				case StackInst::BlockEnd:
				case StackInst::IfEnd:
				case StackInst::LoopEnd: {
					writer.emitScopeEnd(inst->origin);
					break;
				}
				case StackInst::IfElse: {
					writer.emitIfElse(inst->origin->cast<If>());
					break;
				}
				case StackInst::Catch: {
					writer.emitCatch(inst->origin->cast<Try>(), catchIndexStack.back()++);
					break;
				}
				case StackInst::CatchAll: {
					writer.emitCatchAll(inst->origin->cast<Try>());
					break;
				}
				case StackInst::Delegate: {
					writer.emitDelegate(inst->origin->cast<Try>());
					// Delegates end the try, like a TryEnd.
					catchIndexStack.pop_back();
					break;
				}
				default:
					WASM_UNREACHABLE("unexpected op");
				}
			}
			writer.emitFunctionEnd();
		}
	}
}