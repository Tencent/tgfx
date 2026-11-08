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
  /// An array field's element count as a macro or literal (e.g. "CHAIN_SLOT_CAPACITY"), or null
  /// for a scalar field. The chain's per-slot tables are the reason this exists.
  const char* capacity = nullptr;
  /// The dimension condition the entry is conditional on, or null for an unconditional one. The
  /// chain's resource modules and their uniform blocks are selected this way.
  const char* condition = nullptr;
};

/// One fragment input (the vertex stage's output).
struct VaryingDecl {
  const char* glslType = nullptr;
  const char* name = nullptr;
  int location = 0;
  /// When set, printed instead of `location`: a location macro or an expression over dimension
  /// macros (e.g. "NTEX + 1"). The chain's interface places varyings after a dimension-dependent
  /// base, so its locations are expressions rather than numbers.
  const char* locationExpr = nullptr;
  /// The dimension condition the declaration is conditional on, or null for an unconditional one.
  /// Any preprocessor expression is allowed; the emitter only prints it after `#if`.
  const char* condition = nullptr;
};

/// One texture binding of the fragment stage.
struct SamplerDecl {
  /// The full name for a single binding; the common prefix when count > 1 (the instances are
  /// named `<name>0` .. `<name><count-1>`).
  const char* name = nullptr;
  int binding = 0;
  /// How many consecutive bindings to declare (the chain always binds four leaves or none).
  int count = 1;
  /// When set, printed instead of `binding` (a macro or an expression such as "NTEX").
  const char* bindingExpr = nullptr;
  /// The dimension condition the declaration is conditional on, or null for an unconditional one.
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
 * One generated dispatch over a sampler instance group: the interpreters read leaf k through a
 * compile-time sampler pick because GLSL ES and WGSL cannot index a sampler dynamically. The
 * instance count is not repeated here — it is the referenced sampler group's count, so the two
 * cannot drift.
 */
struct InstanceDispatchDecl {
  const char* function = nullptr;
  /// The Samplers entry's name (the group prefix) this dispatch selects among.
  const char* samplerGroup = nullptr;
  const char* returnType = nullptr;
  const char* coordinateType = nullptr;
  /// The swizzle decoder applied to every read, or null when the read is returned verbatim.
  const char* swizzleFunction = nullptr;
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
  /// (xpDstBindingConditional is -1 when the binding is unconditional). A chain's binding is an
  /// expression over the leaf count, so the expression forms take precedence when set.
  int xpDstBinding = 0;
  int xpDstBindingConditional = -1;
  const char* xpDstCondition = nullptr;
  const char* xpDstBindingExpr = nullptr;
  const char* xpDstBindingConditionalExpr = nullptr;
  /// The generated sampler dispatches (one per instance group), or null when none.
  const InstanceDispatchDecl* dispatches = nullptr;
  size_t dispatchCount = 0;
  /// The pipeline template the entry ends with: the dedicated families share the fill pipeline,
  /// the interpreter has its own chain pipeline.
  const char* mainInclude = "pipeline/fill/fill_fragment_main.inc";
  /// The support modules included after the generated interface and before the pipeline template
  /// (the math libraries the entry's interface and evaluation loop depend on), or null.
  const char* const* extraIncludes = nullptr;
  size_t extraIncludeCount = 0;
  /// Modules included before the uniform block (the dimension defaults and the varying-location
  /// tables a conditional interface needs), or null.
  const char* const* prologueIncludes = nullptr;
  size_t prologueIncludeCount = 0;
  /// Tool-generated pragma directives the entry must carry (`tgfx_chain_ops`,
  /// `tgfx_spec_include`); they are replaced by the compile pipeline, never by a driver.
  const char* const* pragmas = nullptr;
  size_t pragmaCount = 0;
  /// When set, the whole uniform block is conditional on this dimension (a family whose only
  /// uniform is the XP block declares it only when HAS_XP is on).
  const char* blockCondition = nullptr;
};

/// One block/interface/step element of a declaration list.
#define TGFX_DECL_ELEMENT(kind, ...) TGFX_DECL_ELEMENT_##kind(__VA_ARGS__)
#define TGFX_DECL_ELEMENT_Field(type, name) \
  { FragmentElementKind::Field, #name, ParamType::type }
#define TGFX_DECL_ELEMENT_ArrayField(type, name, capacity) \
  { FragmentElementKind::Field, #name, ParamType::type, capacity }
#define TGFX_DECL_ELEMENT_Module(path) \
  { FragmentElementKind::Module, path, ParamType::Float }
#define TGFX_DECL_ELEMENT_ConditionalModule(path, condition) \
  { FragmentElementKind::Module, path, ParamType::Float, nullptr, condition }
#define TGFX_DECL_ELEMENT_ConditionalField(type, name, condition) \
  { FragmentElementKind::Field, #name, ParamType::type, nullptr, condition }

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
