# TooTiny
A programming language that is simply too tiny

## Variable integer sizes
According to Github data, these are the approximate usages of different integer sizes and common datatypes
* int8_t: 12M
* uint8_t: 104M
* int16_t: 16M
* uint16_t: 69M
* int32_t (also referred to as int, inflates number): 41M
* uint32_t: 165M
* int64_t: 26M
* uint64_t: 66M
* __int128_t: 171K
* __uint128_t: 365K
* float: 451M
* double: 374M
* boolean: 457M
* std::string: 11M
* std::vector: 21M
These values form the basis of a binary tree used to optimize datatypes
```
__int128_t
__uint128_t
std::string
int8_t
int16_t
std::vector
int64_t
uint64_t
uint16_t
uint8_t
uint32_t
double
float
boolean
int32_t
```

Mine: 490,110,086 bytes
531.231 microseconds 1
493.439 microseconds 2
457.464 microseconds 3
Theirs: 490,040,604 bytes
471.271 microseconds 1
470.158 microseconds 2
454.617 microseconds 3

fuzz_opt.py 13775149378022724097
fuzz_opt.py 1351174022909964813

```
template<ValueWritten VT, typename T = int> void putValue(T value = 0) {
    if constexpr (VT == ValueWritten::Magic) {
      o << int32_t(BinaryConsts::Magic);
    } else if constexpr (VT == ValueWritten::Version) {
      o << int32_t(BinaryConsts::Version);
    } else if constexpr (VT == ValueWritten::SectionSize) {
      // Default U32LEB
      o << int32_t(0);
      o << int8_t(0);
    } else if constexpr (VT == ValueWritten::SectionStart) {
      o << uint8_t(value);
    } else if constexpr (VT == ValueWritten::FunctionIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountMemories) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountTypeGroups) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::Type) {
      o << S32LEB(value);
    } else if constexpr (VT == ValueWritten::NumTypes) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::NumFunctionParams) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::NumStructFields) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountImports) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ExternalKind) {
      o << U32LEB(int32_t(value));
    } else if constexpr (VT == ValueWritten::TypeIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::Mutable) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::Attribute) {
      o << uint8_t(value);
    } else if constexpr (VT == ValueWritten::CountDefinedFunctions) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountGlobals) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ASTNode) {
      o << int8_t(value);
    } else if constexpr (VT == ValueWritten::CountExports) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountMemorySegments) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::MemorySegmentFlags) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountDefinedTables) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountElementSegments) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ElementSegmentFlags) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ElementSegmentType) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ElementSegmentSize) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountDefinedTags) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountFunctionIndices) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::NumFunctionLocals) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountTypeNames) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountTables) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::TableIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::GlobalIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::TagIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ElementSegmentIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountGCFieldTypes) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::NumGCFields) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::GCFieldIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::UserSectionData) {
      o << uint8_t(value);
    } else if constexpr (VT == ValueWritten::NumFeatures) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::FeaturePrefix) {
      o << uint8_t(value);
    } else if constexpr (VT == ValueWritten::DylinkSection) {
      o << U32LEB(wasm->dylinkSection->memorySize);
      o << U32LEB(wasm->dylinkSection->memoryAlignment);
      o << U32LEB(wasm->dylinkSection->tableSize);
      o << U32LEB(wasm->dylinkSection->tableAlignment);
    } else if constexpr (VT == ValueWritten::NumNeededDynlibs) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::RTTDepth) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::HeapType) {
      // TODO: Actually s33
      o << S64LEB(value);
    } else if constexpr (VT == ValueWritten::IndexedHeapType) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::BreakIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::SwitchTargets) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::LocalIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ASTNode32) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::AtomicFenceOrder) {
      o << int8_t(value);
    } else if constexpr (VT == ValueWritten::ConstS32) {
      o << S32LEB(value);
    } else if constexpr (VT == ValueWritten::ConstS64) {
      o << S64LEB(value);
    } else if constexpr (VT == ValueWritten::ConstF32) {
      o << int32_t(value);
    } else if constexpr (VT == ValueWritten::ConstF64) {
      o << int64_t(value);
    } else if constexpr (VT == ValueWritten::ConstV128) {
      std::array<uint8_t, 16> v = value.getv128();
      for (size_t i = 0; i < 16; ++i) {
        o << uint8_t(v[i]);
      }
    } else if constexpr (VT == ValueWritten::NumSelectTypes) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ScratchLocalIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::SIMDIndex) {
      o << uint8_t(value);
    } else if constexpr (VT == ValueWritten::MemorySegmentIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::MemoryIndex) {
      o << int8_t(value);
    } else if constexpr (VT == ValueWritten::InlineBufferSize) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::MemorySizeFlags) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::MemoryGrowFlags) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::StructFieldIndex) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::ArraySize) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::FunctionLocalSize) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::CountNumLocalsByType) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::NumLocalsByType) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::MemoryAccessAlignment) {
      o << U32LEB(value);
    } else if constexpr (VT == ValueWritten::MemoryAccessOffset) {
      if (wasm->memory.is64()) {
        o << U64LEB(value);
      } else {
        o << U32LEB(value);
      }
    }
  }

  template<ValueWritten VT, typename T = void, typename U = int>
  T getValue(U inputValue = 0) {
    if constexpr (VT == ValueWritten::Magic) {
      verifyInt32(inputValue);
    } else if constexpr (VT == ValueWritten::Version) {
      verifyInt32(inputValue);
    } else if constexpr (VT == ValueWritten::SectionSize) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::SectionStart) {
      return (T)getInt8();
    } else if constexpr (VT == ValueWritten::FunctionIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountMemories) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountTypeGroups) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::Type) {
      return (T)getS32LEB();
    } else if constexpr (VT == ValueWritten::NumTypes) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::NumFunctionParams) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::NumStructFields) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountImports) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ExternalKind) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::TypeIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::Mutable) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::Attribute) {
      return (T)getInt8();
    } else if constexpr (VT == ValueWritten::CountDefinedFunctions) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountGlobals) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ASTNode) {
      return (T)getInt8();
    } else if constexpr (VT == ValueWritten::CountExports) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountMemorySegments) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::MemorySegmentFlags) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountDefinedTables) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountElementSegments) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ElementSegmentFlags) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ElementSegmentType) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ElementSegmentSize) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountDefinedTags) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountFunctionIndices) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::NumFunctionLocals) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountTypeNames) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountTables) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::TableIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::GlobalIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::TagIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ElementSegmentIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountGCFieldTypes) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::NumGCFields) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::GCFieldIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::UserSectionData) {
      return (T)getInt8();
    } else if constexpr (VT == ValueWritten::NumFeatures) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::FeaturePrefix) {
      return (T)getInt8();
    } else if constexpr (VT == ValueWritten::DylinkSection) {
      wasm.dylinkSection->memorySize = getU32LEB();
      wasm.dylinkSection->memoryAlignment = getU32LEB();
      wasm.dylinkSection->tableSize = getU32LEB();
      wasm.dylinkSection->tableAlignment = getU32LEB();
    } else if constexpr (VT == ValueWritten::NumNeededDynlibs) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::RTTDepth) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::HeapType) {
      // TODO: Actually s33
      return (T)getS64LEB();
    } else if constexpr (VT == ValueWritten::IndexedHeapType) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::BreakIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::SwitchTargets) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::LocalIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ASTNode32) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::AtomicFenceOrder) {
      return (T)getInt8();
    } else if constexpr (VT == ValueWritten::ConstS32) {
      return (T)Literal(getS32LEB());
    } else if constexpr (VT == ValueWritten::ConstS64) {
      return (T)Literal(getS64LEB());
    } else if constexpr (VT == ValueWritten::ConstF32) {
      return (T)getFloat32Literal();
    } else if constexpr (VT == ValueWritten::ConstF64) {
      return (T)getFloat64Literal();
    } else if constexpr (VT == ValueWritten::ConstV128) {
      return (T)getVec128Literal();
    } else if constexpr (VT == ValueWritten::NumSelectTypes) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ScratchLocalIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::SIMDIndex) {
      return (T)getLaneIndex(inputValue);
    } else if constexpr (VT == ValueWritten::MemorySegmentIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::MemoryIndex) {
      return (T)getInt8();
    } else if constexpr (VT == ValueWritten::InlineBufferSize) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::MemorySizeFlags) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::MemoryGrowFlags) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::StructFieldIndex) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::ArraySize) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::FunctionLocalSize) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::CountNumLocalsByType) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::NumLocalsByType) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::MemoryAccessAlignment) {
      return (T)getU32LEB();
    } else if constexpr (VT == ValueWritten::MemoryAccessOffset) {
      if (wasm.memory.is64()) {
        return (T)getU64LEB();
      } else {
        return (T)getU32LEB();
      }
    }
  }
```