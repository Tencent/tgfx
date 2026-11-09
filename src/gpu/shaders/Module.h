/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "gpu/Uniform.h"

namespace tgfx {

/**
 * One capability of a shader family: the dimension set, the interface elements (attributes,
 * uniform fields, textures, varyings) and the two GLSL hooks. A module declaration is shared by
 * the offline tool (assembly and layout derivation) and the runtime (parameter writing), and
 * carries no code and no math — every formula lives in the module's .vert.glsl/.frag.glsl files.
 */
enum class ShaderType : uint8_t {
  Float,
  Float2,
  Float3,
  Float4,
  Int,
  Int2,
  Int3,
  Int4,
  Mat3,
  Mat4,
};

struct Element {
  enum class Kind : uint8_t {
    Attribute,
    InstanceAttribute,
    VertexParam,
    Param,
    Texture,
    Varying,
  };
  Kind kind = Kind::Param;
  ShaderType type = ShaderType::Float4;
  /// Module-local name; the assembled GLSL and the derived layout spell it <Module>_<name>.
  const char* name = "";
  /// Array length (std140 rounds each element up to 16 bytes).
  uint8_t count = 1;
  /// Index of one of this module's dims; the element exists iff that dim's value is not 0.
  /// -1 means unconditional. whenValue >= 0 narrows the rule to "the dim equals that value", for
  /// the values a plain non-zero test cannot separate (the dst sampler exists only for HAS_XP==1,
  /// while the framebuffer-fetch dialect must not declare it at all).
  int8_t when = -1;
  int8_t whenValue = -1;
  /// Param only: delivered as a specialization constant when active (reserved; no module uses it
  /// before the chain migration).
  bool spec = false;
};

struct ModuleDim {
  /// Emitted as <Module>_<name>; a PRESENT dim with value 0 makes the assembler skip the whole
  /// module (elements and hooks).
  const char* name = "";
  uint8_t count = 0;
};

struct Module {
  const char* name = "";
  const ModuleDim* dims = nullptr;
  uint8_t dimCount = 0;
  const Element* elements = nullptr;
  uint8_t elementCount = 0;
  /// <Name>.vert.glsl defines void <Name>_vertex(inout Vertex v).
  bool hasVertex = false;
  /// <Name>.frag.glsl defines void <Name>_fragment(inout Fragment f).
  bool hasFragment = false;
};

/// The std140 byte size of one element of the type.
constexpr size_t ShaderTypeSize(ShaderType type) {
  switch (type) {
    case ShaderType::Float:
    case ShaderType::Int:
      return 4;
    case ShaderType::Float2:
    case ShaderType::Int2:
      return 8;
    case ShaderType::Float3:
    case ShaderType::Int3:
      return 12;
    case ShaderType::Float4:
    case ShaderType::Int4:
      return 16;
    case ShaderType::Mat3:
      return 48;
    case ShaderType::Mat4:
      return 64;
  }
  return 0;
}

/// The std140 alignment of the type (a three-component vector aligns to 16, like a vec4 slot).
constexpr size_t ShaderTypeAlign(ShaderType type) {
  switch (type) {
    case ShaderType::Float:
    case ShaderType::Int:
      return 4;
    case ShaderType::Float2:
    case ShaderType::Int2:
      return 8;
    case ShaderType::Float3:
    case ShaderType::Int3:
    case ShaderType::Float4:
    case ShaderType::Int4:
    case ShaderType::Mat3:
    case ShaderType::Mat4:
      return 16;
  }
  return 0;
}

/// The bundle's uniform format of the type (the reflection entries store this value).
constexpr UniformFormat ShaderTypeFormat(ShaderType type) {
  switch (type) {
    case ShaderType::Float:
      return UniformFormat::Float;
    case ShaderType::Float2:
      return UniformFormat::Float2;
    case ShaderType::Float3:
      return UniformFormat::Float3;
    case ShaderType::Float4:
      return UniformFormat::Float4;
    case ShaderType::Int:
      return UniformFormat::Int;
    case ShaderType::Int2:
      return UniformFormat::Int2;
    case ShaderType::Int3:
      return UniformFormat::Int3;
    case ShaderType::Int4:
      return UniformFormat::Int4;
    case ShaderType::Mat3:
      return UniformFormat::Float3x3;
    case ShaderType::Mat4:
      return UniformFormat::Float4x4;
  }
  return UniformFormat::Float;
}

/// The GLSL type name of the element in an assembled shader.
inline const char* ShaderTypeGlslName(ShaderType type) {
  switch (type) {
    case ShaderType::Float:
      return "float";
    case ShaderType::Float2:
      return "vec2";
    case ShaderType::Float3:
      return "vec3";
    case ShaderType::Float4:
      return "vec4";
    case ShaderType::Int:
      return "int";
    case ShaderType::Int2:
      return "ivec2";
    case ShaderType::Int3:
      return "ivec3";
    case ShaderType::Int4:
      return "ivec4";
    case ShaderType::Mat3:
      return "mat3";
    case ShaderType::Mat4:
      return "mat4";
  }
  return "float";
}

/// Frame and Xfer are implicit singletons: their dim macros and field names carry no module
/// prefix, so the assembled text spells HAS_XP and DstTextureUpperLeft exactly like the shared
/// xp contract the rest of the pipeline reads.
inline bool ModuleIsImplicit(const Module* module) {
  return std::strcmp(module->name, "Frame") == 0 || std::strcmp(module->name, "Xfer") == 0;
}

/// The assembled name of an element: bare for the implicit modules, <Module>_<name> otherwise.
inline std::string ElementNameOf(const Module* module, const Element& element) {
  if (ModuleIsImplicit(module)) {
    return element.name;
  }
  return std::string(module->name) + "_" + element.name;
}

/// The assembled name of a varying: the v prefix keeps it apart from a same-named attribute.
inline std::string VaryingNameOf(const Module* module, const Element& element) {
  if (ModuleIsImplicit(module)) {
    return std::string("v") + element.name;
  }
  return std::string(module->name) + "_v" + element.name;
}

}  // namespace tgfx
