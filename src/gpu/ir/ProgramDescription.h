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
};

struct AttributeDesc {
  std::string name = {};
};

struct VaryingDesc {
  std::string name = {};
  SLType type = SLType::Void;
};

struct UniformDesc {
  std::string name = {};
};

struct SamplerDesc {
  std::string name = {};
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

/// One program's typed description. This phase carries the vertex stage and the interface; the
/// fragment stage still emits its text through the existing builder until the instruction stream
/// moves here.
struct ProgramDescription {
  InterfaceDesc interface = {};
  std::vector<VertexCall> vertexCalls = {};
};

}  // namespace tgfx
