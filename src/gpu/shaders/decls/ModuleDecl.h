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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include "gpu/shaders/modules/ParamHandle.h"

// The fragment-stage declaration language: one header per family states its uniform block, its
// interface and its step selection, and shader_build_tool's emitter turns the declaration into the
// family's entry file. The declarations carry no code and no math — the family's .glsl module keeps
// every formula — so the emitter is a template filler, never a small compiler.
//
// Two rules keep that property, and shader_build_tool checks both:
//   - a module file never declares an interface (no uniform/layout/in/out/#define);
//   - the emitter never synthesizes an expression (it only prints declared names and paths).
//
// The uniform block's order is the GLSL declaration order, which is the std140 offset order: it is
// part of every artifact's meaning, so a declaration lists its fields in the exact order the
// handwritten block used to.

namespace tgfx {

/// One entry of a fragment family's uniform block.
enum class FragmentElementKind : uint8_t { Field, Module };

/**
 * One uniform-block entry: a field (name and value type) or a shared-declaration include kept
 * verbatim. A module placeholder lets a declaration mix family fields with the shared blocks
 * (clip, XP, texture source) in the order the GLSL requires, without duplicating the shared
 * fields' spelling here.
 */
struct FragmentElement {
  FragmentElementKind kind = FragmentElementKind::Field;
  /// The uniform name for a field, the #include path for a module.
  const char* name = nullptr;
  /// The field's value type; unused by a module placeholder.
  ParamType type = ParamType::Float;
};

/// One fragment input (the vertex stage's output).
struct VaryingDecl {
  const char* glslType = nullptr;
  const char* name = nullptr;
  int location = 0;
  /// The dimension macro the declaration is conditional on, or null for an unconditional one.
  const char* condition = nullptr;
};

/// One texture binding of the fragment stage.
struct SamplerDecl {
  const char* name = nullptr;
  int binding = 0;
  /// The dimension macro the declaration is conditional on, or null for an unconditional one.
  const char* condition = nullptr;
};

/**
 * One FILL_* step selection: the macro name and the symbol it selects. The numeric value stays in
 * fill_fragment_main.inc — the declaration carries only the spelling, so the two cannot drift.
 */
struct StepDecl {
  const char* fillMacro = nullptr;
  const char* valueSymbol = nullptr;
  /// The dimension macro the selection is conditional on, or null for an unconditional one.
  const char* condition = nullptr;
};

/**
 * The fragment entry of one declared family: its identity, the file the loader would read (also
 * the override key the emitter's text replaces), the uniform block, the interface, and the step
 * selection.
 */
struct FragmentFamilyDecl {
  const char* name = nullptr;
  const char* file = nullptr;
  const FragmentElement* block = nullptr;
  size_t blockCount = 0;
  const VaryingDecl* varyings = nullptr;
  size_t varyingCount = 0;
  const SamplerDecl* samplers = nullptr;
  size_t samplerCount = 0;
  const StepDecl* steps = nullptr;
  size_t stepCount = 0;
  /// Top-level defines the block depends on, spelled "NAME=VALUE".
  const char* const* defines = nullptr;
  size_t defineCount = 0;
  /// The XP dst-texture binding: a base value, or both branches of a conditional one
  /// (xpDstBindingConditional is -1 when the binding is unconditional).
  int xpDstBinding = 0;
  int xpDstBindingConditional = -1;
  const char* xpDstCondition = nullptr;
};

/// One block/interface/step element of a declaration list.
#define TGFX_DECL_ELEMENT(kind, ...) TGFX_DECL_ELEMENT_##kind(__VA_ARGS__)
#define TGFX_DECL_ELEMENT_Field(type, name) \
  { FragmentElementKind::Field, #name, ParamType::type }
#define TGFX_DECL_ELEMENT_Module(path) \
  { FragmentElementKind::Module, path, ParamType::Float }

/// The GLSL spelling of one uniform field type. An array kind has no scalar spelling: a family
/// that needs an array extends the declaration instead of guessing a size here.
inline const char* GlslTypeOf(ParamType type) {
  switch (type) {
    case ParamType::Float:
      return "float";
    case ParamType::Float2:
      return "vec2";
    case ParamType::Float4:
      return "vec4";
    case ParamType::Int:
      return "int";
    case ParamType::Int4:
      return "ivec4";
    case ParamType::Float3x3:
      return "mat3";
    case ParamType::Float4x4:
      return "mat4";
    case ParamType::Float4Array:
      break;
  }
  return nullptr;
}

}  // namespace tgfx
