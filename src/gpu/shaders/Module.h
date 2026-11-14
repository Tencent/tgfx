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
  /// Array length (std140 rounds each element up to 16 bytes). Fixed counts only; a length that
  /// follows a capacity dim comes from countByValue instead.
  uint8_t count = 1;
  /// Index of one of this module's dims; -1 means unconditional. When >= 0 the element exists iff
  /// (1u << dimValue) & whenMask — the old single-value and not-zero tests are the masks
  /// (1u << whenValue) and WhenNotZero(dimCount) respectively. A mask of 0 with when >= 0 would be
  /// an element that never exists: the derivation asserts against it.
  int8_t when = -1;
  uint8_t whenMask = 0;
  /// Param only: delivered as a specialization constant when active. A spec element must be
  /// unconditional (its id set is part of the program interface, so a `when` gate could shift it)
  /// and must be an Int or an Int4 array; the derivation asserts both.
  bool spec = false;
  /// When >= 0: the element's array count is countByValue[value of this dim] instead of `count`.
  /// The slot interpreter's two pool arrays are the one shape whose length is a capacity dim's
  /// property (the former CHAIN_SLOT_CAPACITY macro, now a declaration fact). -1 = fixed count.
  int8_t countWhen = -1;
  const uint16_t* countByValue = nullptr;
};

/// The mask for the old "the dim is not zero" test over a dim of the given value count: every bit
/// but bit 0 (a 2-value dim has 0x02, a 3-value dim 0x06, a 5-value dim 0x1E).
constexpr uint8_t WhenNotZero(uint8_t dimCount) {
  return static_cast<uint8_t>(((1u << dimCount) - 1u) & ~1u);
}

/// Whether the element exists in a variant whose `when` dim holds dimValue (dimValue is ignored
/// for unconditional elements).
constexpr bool ElementPresentInVariant(const Element& element, uint8_t dimValue) {
  return element.when < 0 || (element.whenMask & (1u << dimValue)) != 0;
}

/// The element's array count in a variant whose `countWhen` dim holds dimValue (dimValue is
/// ignored for fixed counts). 16 bits because the slot pool outgrows the byte (32 slots * 7 words
/// + the 32-word block region is exactly 256).
constexpr uint16_t ElementCountOf(const Element& element, uint8_t dimValue) {
  if (element.countWhen < 0 || element.countByValue == nullptr) {
    return element.count;
  }
  return element.countByValue[dimValue];
}

/// The draw fact a dimension takes its value from. The declaration names the fact; the match layer
/// evaluates it against the draw's facts, so a rule never spells a dimension by position.
enum class DimFact : uint8_t {
  /// Not annotated yet: the family's rule still supplies this dimension's value, so the
  /// declaration-driven composition refuses the family instead of guessing.
  Unspecified = 0,
  /// Always 0: the dimension exists but this family never varies it.
  Constant0,
  /// The variant slot is free but the family always fixes it to 1.
  Constant1,
  /// The destination color's path: none, dst texture, framebuffer fetch (Xfer's HAS_XP).
  XpType,
  /// The draw carries a device-space mask (DeviceMask's PRESENT).
  DeviceMaskPresent,
  /// The draw carries a local-space mask (LocalMask's PRESENT).
  LocalMaskPresent,
  /// The draw's color graph is one evaluable gradient source (Gradient's PRESENT).
  GradientPresent,
  /// The tiled source needs the full nine-mode recipe (TiledTexture's FULL).
  TiledFull,
  /// The draw supplies a tiled source at all (TiledTexture's PRESENT).
  TiledPresent,
  /// The quad's coordinate comes from a uv attribute (QuadTextureGeometry's HAS_UV_COORD).
  UvCoord,
  /// The source carries a subset rect (QuadTextureGeometry/Texture's HAS_SUBSET).
  Subset,
  /// The geometry carries a uv matrix (RoundStrokeRectGeometry's UV_MATRIX).
  UvMatrix,
  /// The mesh provides texture coordinates (MeshGeometry's TEX_COORDS).
  MeshTexCoords,
  /// The instanced batch carries per-vertex colors (ShapeInstancedGeometry's VERTEX_COLOR).
  VertexColor,
  /// The atlas carries the AA coverage attribute (AtlasTextGeometry's HAS_COVERAGE).
  AtlasCoverage,
  /// The multi-plane source's chroma layout (YUVTexture's FORMAT).
  YuvFormat,
  /// The glass optics' dispersion switch (GlassRefraction's DISPERSION).
  GlassDispersion,
  /// The glass shape form (GlassRefraction's SHAPE).
  GlassShape,
};

struct ModuleDim {
  /// Emitted as <Module>_<name>; a PRESENT dim with value 0 makes the assembler skip the whole
  /// module (elements and hooks).
  const char* name = "";
  uint8_t count = 0;
  /// The draw fact this dim takes its value from. A dim left Unspecified is one whose value the
  /// match rule still spells explicitly; annotating every dim of a family is what lets the
  /// declaration compose its variant on its own.
  DimFact source = DimFact::Unspecified;
};

/// One module's unrolled executor site: the assembler generates the macro <Name>_RUN_UNROLLED_SLOTS
/// (one guarded call per slot of the variant's declared capacity) and the fragment hook invokes it
/// inside the function whose locals `arguments` names — a macro, not a function, so the site stays
/// a statement sequence in the caller's scope, exactly like the former tool pragma's expansion.
struct UnrollSite {
  /// The module-local function that evaluates one slot, e.g. "chainRunSlotAt".
  const char* function = "";
  /// Forwarded verbatim after the slot index (the caller's locals, e.g.
  /// "result, clipRegister, coverageRootRegister").
  const char* arguments = "";
  /// The spec Int4 array element whose variant count bounds the unroll (SlotPacked).
  uint8_t capacityElement = 0;
  /// The spec Int scalar element holding the live slot count (SlotCount).
  uint8_t countElement = 0;
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
  /// The module spells its interface names verbatim (no <Module>_ prefix): uniform, attribute
  /// and varying names, and its dim macros, appear exactly as declared. Frame and Xfer are the
  /// singletons whose names the shared contracts spell bare; the declared Chain modules set it
  /// too, because they reuse the legacy kernel's interface spellings verbatim in the assembled
  /// text. The runtime addresses those fields by element index through the derived layout.
  bool bareNames = false;
  /// Present on the modules that carry an unrolled executor (the chain family); the assembler
  /// emits the site's macro from it.
  const UnrollSite* unroll = nullptr;
  /// The module's shared text reads the chain contract's C++ constants (the operator table and the
  /// slot-pool capacities as OP_* / TGFX_CHAIN_* defines). The build injects them for a family
  /// that declares such a module — the unrolled interpreter (Chain) and the bounded tail
  /// (YUVChainTail) both do, and no other family's emitted bytes move.
  bool chainContract = false;
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

/// A module name the element array does not carry. Only declared, never defined: an odr-use is a
/// link error, so even a runtime lookup with a mistyped name fails to build.
uint8_t ElementNameNotFound();

constexpr bool ElementNameEquals(const char* left, const char* right) {
  while (*left != '\0' && *right != '\0') {
    if (*left != *right) {
      return false;
    }
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

/// The element's index in the module's declaration. Evaluate it in a constant expression (the
/// writers' element constants): a name absent from the array then fails the build instead of
/// silently addressing another element.
constexpr uint8_t ElementIndexOf(const Module& module, const char* name) {
  for (uint8_t index = 0; index < module.elementCount; ++index) {
    if (ElementNameEquals(module.elements[index].name, name)) {
      return index;
    }
  }
  return ElementNameNotFound();
}

/// The index of the element that carries the named uniform field: attributes and varyings never
/// produce one, so the search skips them — a module may spell one name on a varying and a param
/// both (the varying comes first), and the writers address the field. Evaluate it in a constant
/// expression like ElementIndexOf.
constexpr uint8_t UniformElementIndexOf(const Module& module, const char* name) {
  for (uint8_t index = 0; index < module.elementCount; ++index) {
    const Element& element = module.elements[index];
    if (element.kind != Element::Kind::Param && element.kind != Element::Kind::VertexParam) {
      continue;
    }
    if (ElementNameEquals(element.name, name)) {
      return index;
    }
  }
  return ElementNameNotFound();
}

/// Whether the module declares an element of that name: the writers shared by several modules
/// branch on it in constant expressions, so each module writes exactly the fields it declares.
constexpr bool HasElement(const Module& module, const char* name) {
  for (uint8_t index = 0; index < module.elementCount; ++index) {
    if (ElementNameEquals(module.elements[index].name, name)) {
      return true;
    }
  }
  return false;
}

/// The assembled name of an element: verbatim for a bare-names module, <Module>_<name> otherwise.
inline std::string ElementNameOf(const Module* module, const Element& element) {
  if (module->bareNames) {
    return element.name;
  }
  return std::string(module->name) + "_" + element.name;
}

/// The assembled name of a varying: verbatim for a bare-names module (those declare the whole
/// name, e.g. "vColor" and "TransformedCoords_0"), <Module>_v<name> otherwise.
inline std::string VaryingNameOf(const Module* module, const Element& element) {
  if (module->bareNames) {
    return element.name;
  }
  return std::string(module->name) + "_v" + element.name;
}

}  // namespace tgfx
