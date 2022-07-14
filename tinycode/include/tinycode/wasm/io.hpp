#pragma once

#include <fstream>

#include "ir/import-utils.h"
#include "wasm-binary.h"
#include "wasm-debug.h"
#include "wasm-stack.h"
#include "wasm.h"

#include "optimized.hpp"

namespace TinyCode {
	namespace Wasm {
		using namespace wasm;
		using Export = wasm::Export;

		// Writes out wasm to the binary format
		class OptimizedWasmWriter {
			// Computes the indexes in a wasm binary, i.e., with function imports
			// and function implementations sharing a single index space, etc.,
			// and with the imports first (the Module's functions and globals
			// arrays are not assumed to be in a particular order, so we can't
			// just use them directly).
			struct BinaryIndexes {
				std::unordered_map<Name, Index> functionIndexes;
				std::unordered_map<Name, Index> tagIndexes;
				std::unordered_map<Name, Index> globalIndexes;
				std::unordered_map<Name, Index> tableIndexes;
				std::unordered_map<Name, Index> elemIndexes;

				BinaryIndexes(Module& wasm) {
					auto addIndexes = [&](auto& source, auto& indexes) {
						auto addIndex = [&](auto* curr) {
							auto index          = indexes.size();
							indexes[curr->name] = index;
						};
						for(auto& curr : source) {
							if(curr->imported()) {
								addIndex(curr.get());
							}
						}
						for(auto& curr : source) {
							if(!curr->imported()) {
								addIndex(curr.get());
							}
						}
					};
					addIndexes(wasm.functions, functionIndexes);
					addIndexes(wasm.tags, tagIndexes);
					addIndexes(wasm.tables, tableIndexes);

					for(auto& curr : wasm.elementSegments) {
						auto index              = elemIndexes.size();
						elemIndexes[curr->name] = index;
					}

					// Globals may have tuple types in the IR, in which case they lower to
					// multiple globals, one for each tuple element, in the binary. Tuple
					// globals therefore occupy multiple binary indices, and we have to take
					// that into account when calculating indices.
					Index globalCount = 0;
					auto addGlobal    = [&](auto* curr) {
                        globalIndexes[curr->name] = globalCount;
                        globalCount += curr->type.size();
					};
					for(auto& curr : wasm.globals) {
						if(curr->imported()) {
							addGlobal(curr.get());
						}
					}
					for(auto& curr : wasm.globals) {
						if(!curr->imported()) {
							addGlobal(curr.get());
						}
					}
				}
			};

		public:
			OptimizedWasmWriter(Module* input, std::vector<uint8_t>& bytes, uint64_t current_bit)
				: binaryIO(OptimizedWasmBinaryWriter(bytes, current_bit, input))
				, wasm(input)
				, indexes(*input) {
				prepare();
			}

			uint64_t analyzeAndWrite() {
				binaryIO.setHuffmanCreation(true);
				write();
				binaryIO.clear();
				binaryIO.determineWritingSchemes();
				binaryIO.setHuffmanCreation(false);
				write();
				return binaryIO.finish();
			}

			// locations in the output binary for the various parts of the module
			struct TableOfContents {
				struct Entry {
					Name name;
					size_t offset; // where the entry starts
					size_t size;   // the size of the entry
					Entry(Name name, size_t offset, size_t size)
						: name(name)
						, offset(offset)
						, size(size) { }
				};
				std::vector<Entry> functionBodies;
			} tableOfContents;

			void setNamesSection(bool set) {
				debugInfo      = set;
				emitModuleName = set;
			}
			void setEmitModuleName(bool set) {
				emitModuleName = set;
			}
			void setSourceMap(std::ostream* set, std::string url) {
				sourceMap    = set;
				sourceMapUrl = url;
			}
			void setSymbolMap(std::string set) {
				symbolMap = set;
			}

			uint64_t write();
			void writeHeader();
			int32_t writeU32LEBPlaceholder();
			template <typename T> int32_t startSection(T code);
			void finishSection(int32_t start);
			int32_t startSubsection(BinaryConsts::UserSections::Subsection code);
			void finishSubsection(int32_t start);
			void writeStart();
			void writeMemory();
			void writeTypes();
			void writeImports();

			void writeFunctionSignatures();
			void writeExpression(Expression* curr);
			void writeFunctions();
			void writeGlobals();
			void writeExports();
			void writeDataCount();
			void writeDataSegments();
			void writeTags();

			uint32_t getFunctionIndex(Name name) const;
			uint32_t getTableIndex(Name name) const;
			uint32_t getGlobalIndex(Name name) const;
			uint32_t getTagIndex(Name name) const;
			uint32_t getTypeIndex(HeapType type) const;

			void writeTableDeclarations();
			void writeElementSegments();
			void writeNames();
			void writeSourceMapUrl();
			void writeSymbolMap();
			void writeLateUserSections();
			void writeUserSection(const UserSection& section);
			void writeFeaturesSection();
			void writeDylinkSection();
			void writeLegacyDylinkSection();

			void initializeDebugInfo();
			void writeSourceMapProlog();
			void writeSourceMapEpilog();
			void writeDebugLocation(const Function::DebugLocation& loc);
			void writeDebugLocation(Expression* curr, Function* func);
			void writeDebugLocationEnd(Expression* curr, Function* func);
			void writeExtraDebugLocation(Expression* curr, Function* func, size_t id);

			template <ValueWritten VT, typename T = int> void writeValue(T value = 0) {
				binaryIO.writeValue<VT>(value);
			}
			size_t streamAdjustSectionLEB(int32_t start);
			void streamAdjustFunctionLEB(
				Function* func, size_t start, size_t size, size_t sizePos, size_t sourceMapSize);

			// helpers
			void writeEscapedName(const char* name);
			void writeInlineBuffer(const char* data, size_t size);

			struct Buffer {
				const char* data;
				size_t size;
				size_t pointerLocation;
				Buffer(const char* data, size_t size, size_t pointerLocation)
					: data(data)
					, size(size)
					, pointerLocation(pointerLocation) { }
			};

			Module* getModule() {
				return wasm;
			}

			void writeType(Type type);

			// Writes an arbitrary heap type, which may be indexed or one of the
			// basic types like funcref.
			void writeHeapType(HeapType type);
			// Writes an indexed heap type. Note that this is encoded differently than a
			// general heap type because it does not allow negative values for basic heap
			// types.
			void writeIndexedHeapType(HeapType type);

			void writeField(const Field& field);

		private:
			OptimizedWasmBinaryWriter binaryIO;

			Module* wasm;
			BinaryIndexes indexes;
			ModuleUtils::IndexedHeapTypes indexedTypes;

			bool debugInfo = true;

			// TODO: Remove `emitModuleName` in the future once there are better ways to
			// ensure modules have meaningful names in stack traces.For example, using
			// ObjectURLs works in FireFox, but not Chrome. See
			// https://bugs.chromium.org/p/v8/issues/detail?id=11808.
			bool emitModuleName = true;

			std::ostream* sourceMap = nullptr;
			std::string sourceMapUrl;
			std::string symbolMap;

			MixedArena allocator;

			// storage of source map locations until the section is placed at its final
			// location (shrinking LEBs may cause changes there)
			std::vector<std::pair<size_t, const Function::DebugLocation*>> sourceMapLocations;
			size_t sourceMapLocationsSizeAtSectionStart;
			Function::DebugLocation lastDebugLocation;

			std::unique_ptr<ImportInfo> importInfo;

			// General debugging info: track locations as we write.
			BinaryLocations binaryLocations;
			size_t binaryLocationsSizeAtSectionStart;
			// Track the expressions that we added for the current function being
			// written, so that we can update those specific binary locations when
			// the function is written out.
			std::vector<Expression*> binaryLocationTrackedExpressionsForFunc;

			// Maps function names to their mapped locals. This is used when we emit the
			// local names section: we map the locals when writing the function, save that
			// info here, and then use it when writing the names.
			std::unordered_map<Name, MappedLocals> funcMappedLocals;

			void prepare();
		};

		class OptimizedWasmReader {
			Module& wasm;
			MixedArena& allocator;
			OptimizedWasmBinaryReader binaryIO;
			std::istream* sourceMap;
			std::pair<uint32_t, Function::DebugLocation> nextDebugLocation;
			bool debugInfo          = true;
			bool DWARF              = false;
			bool skipFunctionBodies = false;

			Index startIndex = -1;
			std::set<Function::DebugLocation> debugLocation;
			size_t codeSectionLocation;

			std::set<BinaryConsts::Section> seenSections;

			// All types defined in the type section
			std::vector<HeapType> types;

		public:
			OptimizedWasmReader(Module& wasm, FeatureSet features, std::vector<uint8_t>& bytes, uint64_t current_bit)
				: wasm(wasm)
				, allocator(wasm.allocator)
				, binaryIO(OptimizedWasmBinaryReader(bytes, current_bit, wasm))
				, sourceMap(nullptr)
				, nextDebugLocation(0, { 0, 0, 0 })
				, debugLocation() {
				wasm.features = features;
			}

			uint64_t analyzeAndRead() {
				binaryIO.readWritingSchemes();
				return read();
			}

			void setDebugInfo(bool value) {
				debugInfo = value;
			}
			void setDWARF(bool value) {
				DWARF = value;
			}
			void setSkipFunctionBodies(bool skipFunctionBodies_) {
				skipFunctionBodies = skipFunctionBodies_;
			}
			uint64_t read();
			void readUserSection(size_t payloadLen);

			template <ValueWritten VT, typename I = int> auto getValue(I inputValue = 0) {
				return binaryIO.getValue<VT>(inputValue);
			}

			bool getBasicType(int32_t code, Type& out);
			bool getBasicHeapType(int64_t code, HeapType& out);
			// Read a value and get a type for it.
			Type getType();
			// Get a type given the initial S32LEB has already been read, and is provided.
			Type getType(int initial);
			HeapType getHeapType();
			HeapType getIndexedHeapType();

			Type getConcreteType();
			void readHeader();
			void readStart();
			void readMemory();
			void readTypes();

			// gets a name in the combined import+defined space
			Name getFunctionName(Index index);
			Name getTableName(Index index);
			Name getGlobalName(Index index);
			Name getTagName(Index index);

			void readImports();

			// The signatures of each function, including imported functions, given in the
			// import and function sections. Store HeapTypes instead of Signatures because
			// reconstructing the HeapTypes from the Signatures is expensive.
			std::vector<HeapType> functionTypes;

			void readFunctionSignatures();
			HeapType getTypeByIndex(Index index);
			HeapType getTypeByFunctionIndex(Index index);
			Signature getSignatureByTypeIndex(Index index);
			Signature getSignatureByFunctionIndex(Index index);

			size_t nextLabel;

			Name getNextLabel();

			// We read functions and globals before we know their names, so we need to
			// backpatch the names later

			// we store functions here before wasm.addFunction after we know their names
			std::vector<Function*> functions;
			// we store function imports here before wasm.addFunctionImport after we know
			// their names
			std::vector<Function*> functionImports;
			// at index i we have all refs to the function i
			std::map<Index, std::vector<Expression*>> functionRefs;
			Function* currFunction = nullptr;
			// before we see a function (like global init expressions), there is no end of
			// function to check
			Index endOfFunction = -1;

			// we store tables here before wasm.addTable after we know their names
			std::vector<std::unique_ptr<Table>> tables;
			// we store table imports here before wasm.addTableImport after we know
			// their names
			std::vector<Table*> tableImports;
			// at index i we have all references to the table i
			std::map<Index, std::vector<Expression*>> tableRefs;

			std::map<Index, Name> elemTables;

			// we store elems here after being read from binary, until when we know their
			// names
			std::vector<std::unique_ptr<ElementSegment>> elementSegments;

			// we store globals here before wasm.addGlobal after we know their names
			std::vector<std::unique_ptr<Global>> globals;
			// we store global imports here before wasm.addGlobalImport after we know
			// their names
			std::vector<Global*> globalImports;
			// at index i we have all refs to the global i
			std::map<Index, std::vector<Expression*>> globalRefs;

			// Throws a parsing error if we are not in a function context
			void requireFunctionContext(const char* error);

			void readFunctions();
			void readVars();

			std::map<Export*, Index> exportIndices;
			std::vector<Export*> exportOrder;
			void readExports();

			Expression* readExpression();
			void readGlobals();

			struct BreakTarget {
				Name name;
				Type type;
				BreakTarget(Name name, Type type)
					: name(name)
					, type(type) { }
			};
			std::vector<BreakTarget> breakStack;
			// the names that breaks target. this lets us know if a block has breaks to it
			// or not.
			std::unordered_set<Name> breakTargetNames;
			// the names that delegates target.
			std::unordered_set<Name> exceptionTargetNames;

			std::vector<Expression*> expressionStack;

			// Each let block in the binary adds new locals to the bottom of the index
			// space. That is, all previously-existing indexes are bumped to higher
			// indexes. getAbsoluteLocalIndex does this computation.
			// Note that we must track not just the number of locals added in each let,
			// but also the absolute index from which they were allocated, as binaryen
			// will add new locals as it goes for things like stacky code and tuples (so
			// there isn't a simple way to get to the absolute index from a relative one).
			// Hence each entry here is a pair of the number of items, and the absolute
			// index they begin at.
			struct LetData {
				// How many items are defined in this let.
				Index num;
				// The absolute index from which they are allocated from. That is, if num is
				// 5 and absoluteStart is 10, then we use indexes 10-14.
				Index absoluteStart;
			};
			std::vector<LetData> letStack;

			// Given a relative index of a local (the one used in the wasm binary), get
			// the absolute one which takes into account lets, and is the one used in
			// Binaryen IR.
			Index getAbsoluteLocalIndex(Index index);

			// Control flow structure parsing: these have not just the normal binary
			// data for an instruction, but also some bytes later on like "end" or "else".
			// We must be aware of the connection between those things, for debug info.
			std::vector<Expression*> controlFlowStack;

			// Called when we parse the beginning of a control flow structure.
			void startControlFlow(Expression* curr);

			// set when we know code is unreachable in the sense of the wasm spec: we are
			// in a block and after an unreachable element. this helps parse stacky wasm
			// code, which can be unsuitable for our IR when unreachable.
			bool unreachableInTheWasmSense;

			// set when the current code being processed will not be emitted in the
			// output, which is the case when it is literally unreachable, for example,
			// (block $a
			//   (unreachable)
			//   (block $b
			//     ;; code here is reachable in the wasm sense, even though $b as a whole
			//     ;; is not
			//     (unreachable)
			//     ;; code here is unreachable in the wasm sense
			//   )
			// )
			bool willBeIgnored;

			BinaryConsts::ASTNodes lastSeparator = BinaryConsts::End;

			// process a block-type scope, until an end or else marker, or the end of the
			// function
			void processExpressions();
			void skipUnreachableCode();

			void pushExpression(Expression* curr);
			Expression* popExpression();
			Expression* popNonVoidExpression();
			Expression* popTuple(size_t numElems);
			Expression* popTypedExpression(Type type);

			void validateBinary(); // validations that cannot be performed on the Module
			void processNames();

			size_t dataCount  = 0;
			bool hasDataCount = false;

			void readDataSegments();
			void readDataCount();

			void readTableDeclarations();
			void readElementSegments();

			void readTags();

			static Name escape(Name name);
			void readNames(size_t);
			void readFeatures(size_t);
			void readDylink(size_t);
			void readDylink0(size_t);

			// Debug information reading helpers
			void setDebugLocations(std::istream* sourceMap_) {
				sourceMap = sourceMap_;
			}
			std::unordered_map<std::string, Index> debugInfoFileIndices;
			void readNextDebugLocation();
			void readSourceMapHeader();

			// AST reading
			int depth = 0; // only for debugging

			BinaryConsts::ASTNodes readExpression(Expression*& curr);
			void pushBlockElements(Block* curr, Type type, size_t start);
			void visitBlock(Block* curr);

			// Gets a block of expressions. If it's just one, return that singleton.
			Expression* getBlockOrSingleton(Type type);

			auto getBreakTarget(int32_t offset);
			Name getExceptionTargetName(int32_t offset);

			void readMemoryAccess(Address& alignment, Address& offset);

			void visitIf(If* curr);
			void visitLoop(Loop* curr);
			void visitBreak(Break* curr, uint8_t code);
			void visitSwitch(Switch* curr);
			void visitCall(Call* curr);
			void visitCallIndirect(CallIndirect* curr);
			void visitLocalGet(LocalGet* curr);
			void visitLocalSet(LocalSet* curr, uint8_t code);
			void visitGlobalGet(GlobalGet* curr);
			void visitGlobalSet(GlobalSet* curr);
			bool maybeVisitLoad(Expression*& out, uint8_t code, bool isAtomic);
			bool maybeVisitStore(Expression*& out, uint8_t code, bool isAtomic);
			bool maybeVisitNontrappingTrunc(Expression*& out, uint32_t code);
			bool maybeVisitAtomicRMW(Expression*& out, uint8_t code);
			bool maybeVisitAtomicCmpxchg(Expression*& out, uint8_t code);
			bool maybeVisitAtomicWait(Expression*& out, uint8_t code);
			bool maybeVisitAtomicNotify(Expression*& out, uint8_t code);
			bool maybeVisitAtomicFence(Expression*& out, uint8_t code);
			bool maybeVisitConst(Expression*& out, uint8_t code);
			bool maybeVisitUnary(Expression*& out, uint8_t code);
			bool maybeVisitBinary(Expression*& out, uint8_t code);
			bool maybeVisitTruncSat(Expression*& out, uint32_t code);
			bool maybeVisitSIMDBinary(Expression*& out, uint32_t code);
			bool maybeVisitSIMDUnary(Expression*& out, uint32_t code);
			bool maybeVisitSIMDConst(Expression*& out, uint32_t code);
			bool maybeVisitSIMDStore(Expression*& out, uint32_t code);
			bool maybeVisitSIMDExtract(Expression*& out, uint32_t code);
			bool maybeVisitSIMDReplace(Expression*& out, uint32_t code);
			bool maybeVisitSIMDShuffle(Expression*& out, uint32_t code);
			bool maybeVisitSIMDTernary(Expression*& out, uint32_t code);
			bool maybeVisitSIMDShift(Expression*& out, uint32_t code);
			bool maybeVisitSIMDLoad(Expression*& out, uint32_t code);
			bool maybeVisitSIMDLoadStoreLane(Expression*& out, uint32_t code);
			bool maybeVisitMemoryInit(Expression*& out, uint32_t code);
			bool maybeVisitDataDrop(Expression*& out, uint32_t code);
			bool maybeVisitMemoryCopy(Expression*& out, uint32_t code);
			bool maybeVisitMemoryFill(Expression*& out, uint32_t code);
			bool maybeVisitTableSize(Expression*& out, uint32_t code);
			bool maybeVisitTableGrow(Expression*& out, uint32_t code);
			bool maybeVisitI31New(Expression*& out, uint32_t code);
			bool maybeVisitI31Get(Expression*& out, uint32_t code);
			bool maybeVisitRefTest(Expression*& out, uint32_t code);
			bool maybeVisitRefCast(Expression*& out, uint32_t code);
			bool maybeVisitBrOn(Expression*& out, uint32_t code);
			bool maybeVisitRttCanon(Expression*& out, uint32_t code);
			bool maybeVisitRttSub(Expression*& out, uint32_t code);
			bool maybeVisitStructNew(Expression*& out, uint32_t code);
			bool maybeVisitStructGet(Expression*& out, uint32_t code);
			bool maybeVisitStructSet(Expression*& out, uint32_t code);
			bool maybeVisitArrayNew(Expression*& out, uint32_t code);
			bool maybeVisitArrayInit(Expression*& out, uint32_t code);
			bool maybeVisitArrayGet(Expression*& out, uint32_t code);
			bool maybeVisitArraySet(Expression*& out, uint32_t code);
			bool maybeVisitArrayLen(Expression*& out, uint32_t code);
			bool maybeVisitArrayCopy(Expression*& out, uint32_t code);
			void visitSelect(Select* curr, uint8_t code);
			void visitReturn(Return* curr);
			void visitMemorySize(MemorySize* curr);
			void visitMemoryGrow(MemoryGrow* curr);
			void visitNop(Nop* curr);
			void visitUnreachable(Unreachable* curr);
			void visitDrop(Drop* curr);
			void visitRefNull(RefNull* curr);
			void visitRefIs(RefIs* curr, uint8_t code);
			void visitRefFunc(RefFunc* curr);
			void visitRefEq(RefEq* curr);
			void visitTableGet(TableGet* curr);
			void visitTableSet(TableSet* curr);
			void visitTryOrTryInBlock(Expression*& out);
			void visitThrow(Throw* curr);
			void visitRethrow(Rethrow* curr);
			void visitCallRef(CallRef* curr);
			void visitRefAs(RefAs* curr, uint8_t code);
			// Let is lowered into a block.
			void visitLet(Block* curr);

			void throwError(std::string text);

			// Struct/Array instructions have an unnecessary heap type that is just for
			// validation (except for the case of unreachability, but that's not a problem
			// anyhow, we can ignore it there). That is, we also have a reference / rtt
			// child from which we can infer the type anyhow, and we just need to check
			// that type is the same.
			void validateHeapTypeUsingChild(Expression* child, HeapType heapType);

		private:
			bool hasDWARFSections();
		};

		class NewBinaryInstWriter : public OverriddenVisitor<NewBinaryInstWriter> {
		public:
			NewBinaryInstWriter(OptimizedWasmWriter& parent, Function* func, bool sourceMap, bool DWARF)
				: parent(parent)
				, func(func)
				, sourceMap(sourceMap)
				, DWARF(DWARF) { }

			void visit(Expression* curr) {
				if(func && !sourceMap) {
					parent.writeDebugLocation(curr, func);
				}
				OverriddenVisitor<NewBinaryInstWriter>::visit(curr);
				if(func && !sourceMap) {
					parent.writeDebugLocationEnd(curr, func);
				}
			}

			template <ValueWritten VT, typename T = int> void writeValue(T value = 0) {
				parent.writeValue<VT>(value);
			}

#define DELEGATE(CLASS_TO_VISIT) void visit##CLASS_TO_VISIT(CLASS_TO_VISIT* curr);

#include "wasm-delegations.def"

			void emitResultType(Type type);
			void emitIfElse(If* curr);
			void emitCatch(Try* curr, Index i);
			void emitCatchAll(Try* curr);
			void emitDelegate(Try* curr);
			// emit an end at the end of a block/loop/if/try
			void emitScopeEnd(Expression* curr);
			// emit an end at the end of a function
			void emitFunctionEnd();
			void emitUnreachable();
			void mapLocalsAndEmitHeader();

			MappedLocals mappedLocals;

		private:
			void emitMemoryAccess(size_t alignment, size_t bytes, uint32_t offset);
			int32_t getBreakIndex(Name name);

			OptimizedWasmWriter& parent;
			Function* func = nullptr;
			bool sourceMap;
			bool DWARF;

			std::vector<Name> breakStack;

			// The types of locals in the compact form, in order.
			std::vector<Type> localTypes;
			// type => number of locals of that type in the compact form
			std::unordered_map<Type, size_t> numLocalsByType;

			void noteLocalType(Type type);

			// Keeps track of the binary index of the scratch locals used to lower
			// tuple.extract.
			InsertOrderedMap<Type, Index> scratchLocals;
			void countScratchLocals();
			void setScratchLocals();
		};

		// Takes binaryen IR and converts it to something else (binary or stack IR)
		template <typename SubType> class NewBinaryenIRWriter : public Visitor<NewBinaryenIRWriter<SubType>> {
		public:
			NewBinaryenIRWriter(Function* func)
				: func(func) { }

			void write();

			// visits a node, emitting the proper code for it
			void visit(Expression* curr);

			void visitBlock(Block* curr);
			void visitIf(If* curr);
			void visitLoop(Loop* curr);
			void visitTry(Try* curr);

		protected:
			Function* func = nullptr;

		private:
			void emit(Expression* curr) {
				static_cast<SubType*>(this)->emit(curr);
			}
			void emitHeader() {
				static_cast<SubType*>(this)->emitHeader();
			}
			void emitIfElse(If* curr) {
				static_cast<SubType*>(this)->emitIfElse(curr);
			}
			void emitCatch(Try* curr, Index i) {
				static_cast<SubType*>(this)->emitCatch(curr, i);
			}
			void emitCatchAll(Try* curr) {
				static_cast<SubType*>(this)->emitCatchAll(curr);
			}
			void emitDelegate(Try* curr) {
				static_cast<SubType*>(this)->emitDelegate(curr);
			}
			void emitScopeEnd(Expression* curr) {
				static_cast<SubType*>(this)->emitScopeEnd(curr);
			}
			void emitFunctionEnd() {
				static_cast<SubType*>(this)->emitFunctionEnd();
			}
			void emitUnreachable() {
				static_cast<SubType*>(this)->emitUnreachable();
			}
			void emitDebugLocation(Expression* curr) {
				static_cast<SubType*>(this)->emitDebugLocation(curr);
			}
			void visitPossibleBlockContents(Expression* curr);
		};

		// Binaryen IR to binary writer
		class NewBinaryenIRToBinaryWriter : public NewBinaryenIRWriter<NewBinaryenIRToBinaryWriter> {
		public:
			NewBinaryenIRToBinaryWriter(
				OptimizedWasmWriter& parent, Function* func = nullptr, bool sourceMap = false, bool DWARF = false)
				: NewBinaryenIRWriter<NewBinaryenIRToBinaryWriter>(func)
				, parent(parent)
				, writer(parent, func, sourceMap, DWARF)
				, sourceMap(sourceMap)
				, func(func) { }

			void visit(Expression* curr) {
				NewBinaryenIRWriter<NewBinaryenIRToBinaryWriter>::visit(curr);
			}

			void emit(Expression* curr) {
				writer.visit(curr);
			}
			void emitHeader() {
				if(func->prologLocation.size()) {
					parent.writeDebugLocation(*func->prologLocation.begin());
				}
				writer.mapLocalsAndEmitHeader();
			}
			void emitIfElse(If* curr) {
				writer.emitIfElse(curr);
			}
			void emitCatch(Try* curr, Index i) {
				writer.emitCatch(curr, i);
			}
			void emitCatchAll(Try* curr) {
				writer.emitCatchAll(curr);
			}
			void emitDelegate(Try* curr) {
				writer.emitDelegate(curr);
			}
			void emitScopeEnd(Expression* curr) {
				writer.emitScopeEnd(curr);
			}
			void emitFunctionEnd() {
				if(func->epilogLocation.size()) {
					parent.writeDebugLocation(*func->epilogLocation.begin());
				}
				writer.emitFunctionEnd();
			}
			void emitUnreachable() {
				writer.emitUnreachable();
			}
			void emitDebugLocation(Expression* curr) {
				if(sourceMap) {
					parent.writeDebugLocation(curr, func);
				}
			}

			MappedLocals& getMappedLocals() {
				return writer.mappedLocals;
			}

		private:
			OptimizedWasmWriter& parent;
			NewBinaryInstWriter writer;
			bool sourceMap;
			Function* func;
		};

		// Binaryen IR to stack IR converter
		// Queues the expressions linearly in Stack IR (SIR)
		class NewStackIRGenerator : public NewBinaryenIRWriter<NewStackIRGenerator> {
		public:
			NewStackIRGenerator(Module& module, Function* func)
				: NewBinaryenIRWriter<NewStackIRGenerator>(func)
				, module(module) { }

			void emit(Expression* curr);
			void emitScopeEnd(Expression* curr);
			void emitHeader() { }
			void emitIfElse(If* curr) {
				stackIR.push_back(makeStackInst(StackInst::IfElse, curr));
			}
			void emitCatch(Try* curr, Index i) {
				stackIR.push_back(makeStackInst(StackInst::Catch, curr));
			}
			void emitCatchAll(Try* curr) {
				stackIR.push_back(makeStackInst(StackInst::CatchAll, curr));
			}
			void emitDelegate(Try* curr) {
				stackIR.push_back(makeStackInst(StackInst::Delegate, curr));
			}
			void emitFunctionEnd() { }
			void emitUnreachable() {
				stackIR.push_back(makeStackInst(Builder(module).makeUnreachable()));
			}
			void emitDebugLocation(Expression* curr) { }

			StackIR& getStackIR() {
				return stackIR;
			}

		private:
			StackInst* makeStackInst(StackInst::Op op, Expression* origin);
			StackInst* makeStackInst(Expression* origin) {
				return makeStackInst(StackInst::Basic, origin);
			}

			Module& module;
			StackIR stackIR; // filled in write()
		};

		// Stack IR to binary writer
		class NewStackIRToBinaryWriter {
		public:
			NewStackIRToBinaryWriter(OptimizedWasmWriter& parent, Function* func)
				: writer(parent, func, false /* sourceMap */, false /* DWARF */)
				, func(func) { }

			void write();

			MappedLocals& getMappedLocals() {
				return writer.mappedLocals;
			}

		private:
			NewBinaryInstWriter writer;
			Function* func;
		};
	}
}