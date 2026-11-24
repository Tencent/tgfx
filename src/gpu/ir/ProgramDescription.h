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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/SLType.h"

namespace tgfx {

using AttributeID = uint16_t;
using UniformID = uint16_t;
using VaryingID = uint16_t;
using SamplerID = uint16_t;
constexpr VaryingID INVALID_VARYING = static_cast<VaryingID>(-1);

/// The vertex-stage write one record carries. The GLSL printer and the later SPIR-V emitters read
/// the same records; a record names the values exactly as the emitting processor spelled them.
enum class VertexCallKind : uint8_t {
  /// dst = src, an attribute or a prior local stored into a varying.
  StoreVarying,
  /// dst = (matrix * vec3(src, 1)).xy, or the perspective-divided form when hasPerspective.
  TransformPoint,
  /// dst = matrix * vec3(src, 1) written to a varying.
  TransformVarying,
  /// dst = (matrix * vec3(src, 1)).xy + extra.
  TransformPointAdd,
  /// dst = src * operand, two vec2 values.
  MultiplyVec2,
  /// dst = src.xy or src.zw: the two halves of a rect attribute.
  SwizzleVec2,
  /// dst = vec4(src, operand), two vec2 values side by side.
  ComposeVec4,
  /// src = vec4(min(src.xy, src.zw), max(src.xy, src.zw)), in place.
  NormalizeRect,
  /// gl_Position = vec4(src.xy * RTAdjust.xz + RTAdjust.yw, 0, 1).
  StorePosition,
};

/// One vertex-stage write in emission order. The string fields are the names the emitting
/// processor spelled; the printer resolves them to text, and a later typed lowering can replace
/// them with ValueIDs without changing the record's order.
struct VertexCall {
  VertexCallKind kind = VertexCallKind::StoreVarying;
  std::string dst = {};
  std::string src = {};
  std::string operand = {};
  std::string extra = {};
  bool hasPerspective = false;
  /// MultiplyVec2's declaration form: true spells "highp vec2 dst = ...".
  bool declaresResult = false;
  /// The value type the primitive writes; Void when the primitive does not determine it.
  SLType resultType = SLType::Void;
  /// The varying this write targets, resolved after the interface is declared.
  VaryingID varying = INVALID_VARYING;
};

struct AttributeDesc {
  std::string name = {};
  AttributeID id = 0;
};

struct VaryingDesc {
  std::string name = {};
  SLType type = SLType::Void;
  VaryingID id = 0;
  uint8_t location = 0;
  bool produced = false;
};

struct UniformDesc {
  std::string name = {};
  UniformID id = 0;
};

struct SamplerDesc {
  std::string name = {};
  SamplerID id = 0;
};

/// The stage interface the emitter declared: attributes, varyings, uniforms and samplers, in
/// declaration order. The GL printer reads the same handlers for now; the description is the
/// single place later backends will read.
struct InterfaceDesc {
  std::vector<AttributeDesc> attributes = {};
  std::vector<VaryingDesc> varyings = {};
  std::vector<UniformDesc> uniforms = {};
  std::vector<SamplerDesc> samplers = {};
};

enum class FragmentInstructionKind : uint8_t {
  /// A statement chunk the existing FP emission produced; replayed verbatim.
  RawText,
  /// A structured call the FP emission recorded.
  Call,
  Alias,
  StoreColor,
  StoreCoverage,
  Combine,
  Project,
  Root,
};

struct FragmentInstruction {
  FragmentInstructionKind kind = FragmentInstructionKind::RawText;
  /// The call's or store's output variable.
  std::string output = {};
  /// The entry function name.
  std::string function = {};
  /// The call's or store's value operand.
  std::string src = {};
  /// The call arguments, in call order.
  std::vector<std::string> args = {};
  /// The optional wrapper the call site applies around the function result.
  std::string wrapType = {};
  /// The coverage operand of a combine.
  std::string operand = {};
  /// The varying a projected coordinate reads, when the instruction is Project.
  VaryingID varying = INVALID_VARYING;
  /// The alias declaration spelling ("vec4", "highp vec2").
  std::string declaration = {};
  /// A coverage store wraps a scalar source in vec4(...).
  bool splat = false;
  /// A temp call declares its result ("vec4 temp = ...").
  bool declaresResult = false;
  /// The declaration type of a temp call.
  std::string resultType = {};
  /// The statement text, already indented exactly as the inline emission spelled it. The printer
  /// uses its leading whitespace as the statement prefix and rebuilds the statement from fields.
  std::string text = {};
};

/// The fragment stage's recorded instructions, in emission order. This phase keeps every existing
/// statement as a raw chunk and records the structured calls beside them; the printer replays the
/// raw chunks, so the output stays byte-identical while the call metadata is available to the
/// typed lowering that follows.
struct FragmentProgram {
  std::vector<FragmentInstruction> instructions = {};
};

/// One program's typed description. The vertex stage and interface are structured; the fragment
/// stage carries the recorded instructions above.
struct ProgramDescription {
  InterfaceDesc interface = {};
  std::vector<VertexCall> vertexCalls = {};
  FragmentProgram fragment = {};
};

}  // namespace tgfx
