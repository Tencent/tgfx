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
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/shaders/contract/ChainISA.h"

namespace tgfx {

/// How a fragment's code is spelled.
enum class FragmentKind : uint8_t {
  /// A pure function in glsl/ops (or a shared function like xpBlendColors) the call sites name.
  Function,
  /// An inline expression the assembler emits directly (InputOpaque, MulAlpha); no file.
  Expression,
  /// A shared evaluator coupled to the entry's declarations (Gradient, PerlinNoise): the
  /// assembler calls it like the interpreter does until their pure split lands.
  EntryCoupled,
};

/// One call-site argument of a fragment invocation.
enum class FragmentArgKind : uint8_t {
  Input0,
  Input1,
  /// A parameter from the fragment's table, by index.
  Param,
  /// A structural literal fixed by the composition (selector bits, modes); never a uniform.
  Literal,
  /// An entry-computed value the assembler names (the fetched leaf, the fragment coordinate).
  EntryValue,
};

struct FragmentArg {
  FragmentArgKind kind = FragmentArgKind::Input0;
  uint16_t index = 0;
};

/// A parameter's storage type, in whole std140 vec4 words: scalars and vectors own one word,
/// Matrix3 three, Matrix4 four. The declaration layer's ShaderType maps onto this at the adapter.
enum class FragmentType : uint8_t {
  Float,
  Float2,
  Float3,
  Float4,
  Int,
  Int4,
  Matrix3,
  Matrix4,
};

/// One parameter a fragment reads: the semantic names its value source (the CPU reader and the
/// uniform allocation key), the type its std140 footprint.
struct FragmentParam {
  const char* semantic = "";
  FragmentType type = FragmentType::Float;
  uint16_t arraySize = 1;
  /// Program-level parameters live in chain-wide blocks shared by every slot (Gradient,
  /// ColorSpaceXform), not per-slot regions.
  bool programLevel = false;
};

/// One op's fragment contract: the function the call sites name, the parameters it reads, the
/// argument marshalling of one invocation, and the files the function depends on. The GLSL side
/// lives in glsl/ops (pure: no uniforms, samplers, globals or specialization surface); the build
/// compiles every entry against its declared signature, so the table and the sources cannot
/// drift apart silently.
struct FragmentDef {
  int op = 0;
  FragmentKind kind = FragmentKind::Function;
  const char* functionName = "";
  /// The fragment's own source file, root-relative to the shader directory: what the build packs
  /// into the bundle's fragment sources and the assembler includes verbatim. Null for inline
  /// expressions and macro-based operators with no single home.
  const char* sourceFile = nullptr;
  std::vector<FragmentParam> params = {};
  std::vector<FragmentArg> args = {};
  std::vector<const char*> deps = {};
};

/// The fragment table, one entry per chain operator the assembler covers. Returns nullptr for an
/// operator with no fragment (the caller keeps the interpreter path).
const FragmentDef* FindFragment(int op);

/// The fragment at the table's index, for walking the whole manifest (the build's pack emitter).
const FragmentDef* FragmentAt(size_t index);

/// The pack's non-op entries, at fixed ids shared by the build and the runtime: the generated
/// loaders (the chain-ops pragma's expansion), the tail template, and the shared dependency files
/// the op fragments call into.
inline constexpr uint32_t FragmentPackTail = 0x40000000u | 0x1002u;

inline constexpr uint32_t FragmentPackDepAnalyticClipMath = 0x40000000u | 0x2000u;
inline constexpr uint32_t FragmentPackDepTextureSampling = 0x40000000u | 0x2008u;
inline constexpr uint32_t FragmentPackDepTextureSwizzle = 0x40000000u | 0x2001u;
inline constexpr uint32_t FragmentPackDepLeafFetch = 0x40000000u | 0x2002u;

/// The xfer support, packed once per destination-color path with its conditionals resolved at
/// build (an unresolved `#if HAS_XP` would self-mute at the driver): 1 reads the dst through the
/// texture, 2 through framebuffer fetch.
inline constexpr uint32_t FragmentPackXpDstTexture = 0x40000000u | 0x1010u;
inline constexpr uint32_t FragmentPackXpFramebufferFetch = 0x40000000u | 0x1011u;

/// The gradient evaluator, packed in two forms with its one `#if NTEX > 0` branch resolved at
/// build (the driver rejects expression conditionals in the assembled text): the LUT form for a
/// variant that declares leaf samplers, the simple form otherwise.
inline constexpr uint32_t FragmentPackGradientLut = 0x40000000u | 0x1020u;
inline constexpr uint32_t FragmentPackGradientSimple = 0x40000000u | 0x1021u;

/// The plain leaf fetch, packed in two forms with its one `#if HAS_SUBSET` branch resolved at
/// build (the driver rejects `#if` directives): the strict form for a variant that declares the
/// per-quad subset varying, the plain form otherwise.
inline constexpr uint32_t FragmentPackLeafFetchStrict = 0x40000000u | 0x1022u;
inline constexpr uint32_t FragmentPackLeafFetchPlain = 0x40000000u | 0x1023u;

/// The Vulkan linking route's op library, one patched SPIR-V module per pure operator: the wrapper
/// compiled from the same sources the text pack ships, its operator exported for linking and its
/// entry stripped. The per-operator id is the base with the ChainISA value. The resource-coupled
/// evaluators (Gradient, PerlinNoise, ColorSpaceXform) join once their pure split lands.
inline constexpr uint32_t FragmentPackSPIRVOpBase = 0x80000000u;

/// The count of registered fragments (the full op coverage check walks it).
size_t FragmentCount();

}  // namespace tgfx
