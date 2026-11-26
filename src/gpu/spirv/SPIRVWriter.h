/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {
namespace spirv {

// The opcodes, decorations and enumerants the emitters touch.
enum : uint16_t {
  kOpName = 5,
  kOpMemberName = 6,
  kOpExtInstImport = 11,
  kOpExtInst = 12,
  kOpMemoryModel = 14,
  kOpEntryPoint = 15,
  kOpExecutionMode = 16,
  kOpCapability = 17,
  kOpTypeVoid = 19,
  kOpTypeBool = 20,
  kOpTypeInt = 21,
  kOpTypeFloat = 22,
  kOpTypeVector = 23,
  kOpTypeMatrix = 24,
  kOpTypeImage = 25,
  kOpTypeSampler = 26,
  kOpTypeSampledImage = 27,
  kOpTypeArray = 28,
  kOpTypeStruct = 30,
  kOpTypePointer = 32,
  kOpTypeFunction = 33,
  kOpConstant = 43,
  kOpConstantComposite = 44,
  kOpFunction = 54,
  kOpFunctionParameter = 55,
  kOpFunctionEnd = 56,
  kOpFunctionCall = 57,
  kOpVariable = 59,
  kOpLoad = 61,
  kOpStore = 62,
  kOpAccessChain = 65,
  kOpDecorate = 71,
  kOpMemberDecorate = 72,
  kOpVectorShuffle = 79,
  kOpCompositeConstruct = 80,
  kOpCompositeExtract = 81,
  kOpImageSampleImplicitLod = 87,
  kOpFAdd = 129,
  kOpFSub = 130,
  kOpFMul = 133,
  kOpFDiv = 136,
  kOpVectorTimesScalar = 142,
  kOpMatrixTimesVector = 145,
  kOpDot = 148,
  kOpSelect = 169,
  kOpIEqual = 170,
  kOpINotEqual = 171,
  kOpFOrdLessThan = 184,
  kOpFUnordLessThan = 185,
  kOpFOrdGreaterThan = 186,
  kOpSelectionMerge = 247,
  kOpLoopMerge = 246,
  kOpLabel = 248,
  kOpBranch = 249,
  kOpBranchConditional = 250,
  kOpConvertSToF = 123,
  kOpConvertFToS = 129,
  kOpIAdd = 128,
  kOpSLessThanEqual = 133,
  kOpReturn = 253,
  kOpReturnValue = 254,
};
enum : uint32_t {
  kDecorationBlock = 2,
  kDecorationColMajor = 5,
  kDecorationArrayStride = 6,
  kDecorationMatrixStride = 7,
  kDecorationBuiltIn = 11,
  kDecorationLocation = 30,
  kDecorationBinding = 33,
  kDecorationDescriptorSet = 34,
  kDecorationOffset = 35,
  kDecorationLinkageAttributes = 41,
};
enum : uint32_t { kCapabilityShader = 1, kCapabilityLinkage = 5 };
enum : uint32_t { kExecutionModelVertex = 0, kExecutionModelFragment = 4 };
enum : uint32_t { kExecutionModeOriginUpperLeft = 7 };
enum : uint32_t { kBuiltInPosition = 0, kBuiltInFragCoord = 15 };
enum : uint32_t {
  kStorageUniformConstant = 0,
  kStorageInput = 1,
  kStorageUniform = 2,
  kStorageOutput = 3,
  kStorageFunction = 7,
};
enum : uint32_t { kLinkageTypeExport = 0, kLinkageTypeImport = 1 };
enum : uint32_t { kGLSLStd450FMin = 37, kGLSLStd450FMax = 40 };
enum : uint32_t { kImageDim2D = 1 };

/**
 * The module's logical sections: SPIR-V requires capabilities, extended-instruction imports, the
 * memory model and entry points, then annotations, then types and constants, then functions —
 * instructions must appear in that order or no validator accepts the module. The writer routes
 * each instruction into its section as it is emitted, so the emission code can interleave types,
 * decorations and entry points freely.
 */
struct Writer {
  std::vector<uint32_t> pre = {};    // capabilities and the memory model
  std::vector<uint32_t> mode = {};   // entry points and execution modes
  std::vector<uint32_t> debug = {};  // names
  std::vector<uint32_t> ann = {};    // decorations
  std::vector<uint32_t> decl = {};   // types, constants, variables
  std::vector<uint32_t> func = {};   // functions
  std::vector<uint32_t>* sink = &pre;
  uint32_t nextId = 1;

  uint32_t NewId() {
    return nextId++;
  }

  void Sink(std::vector<uint32_t>* section) {
    sink = section;
  }

  void Inst(uint16_t opcode, const std::vector<uint32_t>& operands) {
    sink->push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | opcode);
    sink->insert(sink->end(), operands.begin(), operands.end());
  }

  void Inst(uint16_t opcode) {
    Inst(opcode, {});
  }

  void Finish(std::vector<uint32_t>* out) const {
    out->clear();
    out->reserve(5 + pre.size() + mode.size() + debug.size() + ann.size() + decl.size() +
                 func.size());
    out->push_back(0x07230203u);
    out->push_back(0x00010000u);  // SPIR-V 1.0, the op modules' version
    out->push_back(0);
    out->push_back(nextId);
    out->push_back(0);
    out->insert(out->end(), pre.begin(), pre.end());
    out->insert(out->end(), mode.begin(), mode.end());
    out->insert(out->end(), debug.begin(), debug.end());
    out->insert(out->end(), ann.begin(), ann.end());
    out->insert(out->end(), decl.begin(), decl.end());
    out->insert(out->end(), func.begin(), func.end());
  }
};

/// The canonical type ids one module carries, created once by DeclareCanonicalTypes.
struct Types {
  uint32_t voidId = 0;
  uint32_t boolId = 0;
  uint32_t boolVec4 = 0;
  uint32_t intId = 0;
  uint32_t floatId = 0;
  uint32_t vec2 = 0;
  uint32_t vec3 = 0;
  uint32_t vec4 = 0;
  uint32_t ivec4 = 0;
  uint32_t mat3 = 0;
  uint32_t mat4 = 0;
  uint32_t vec4Array8 = 0;
  uint32_t image = 0;
  uint32_t samplerType = 0;
  uint32_t sampledImage = 0;
};

/// Pads a name to the four-byte word the instruction stream carries.
inline std::vector<uint32_t> NameWords(const char* name) {
  std::string padded = name;
  padded.push_back('\0');
  while (padded.size() % 4 != 0) {
    padded.push_back('\0');
  }
  std::vector<uint32_t> words(padded.size() / 4, 0);
  for (size_t index = 0; index < padded.size(); ++index) {
    words[index / 4] |= static_cast<uint32_t>(static_cast<uint8_t>(padded[index]))
                        << ((index % 4) * 8);
  }
  return words;
}

}  // namespace spirv
}  // namespace tgfx
