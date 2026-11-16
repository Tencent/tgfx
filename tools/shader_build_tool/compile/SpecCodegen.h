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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "core/Diagnostics.h"
#include "core/Types.h"
#include "gpu/shaders/contract/SpecContract.h"

namespace tgfx {

/// One freezable field the scan found in the template: name and whether its accessor takes an
/// index argument (an ivec4 array read) or not (a scalar int read).
struct SpecUsageField {
  std::string name;
  bool indexed = false;
};

/// What the scan found in one family's expanded fragment template.
struct SpecUsage {
  /// The `#pragma tgfx_spec_include` marker is present, so the tool generates and injects the
  /// accessors; without it the family never specializes and every source below is the template.
  bool present = false;
  /// The SPEC_<name>() fields in first-use order (SPEC_ACTIVE is not a field and stays out).
  std::vector<SpecUsageField> fields;
  /// The declared gates, in first-seen order.
  std::vector<SpecGateInfo> gates;
};

/// Scans one family's expanded fragment template. Collects the include marker and the
/// SPEC_<name>() call sites, and reports spec.raw-read: a spec'd uniform name appearing outside
/// its UBO declaration line and outside the SPEC_ call itself reads the uniform directly, so the
/// read escapes the folding a specialized compile performs.

/// Maps the scanned fields onto one variant's reflected uniform block. Every name must exist as
/// an int or ivec4 member (reported as spec.unknown-field otherwise); ids are assigned
/// deterministically in scan order, 0 being the master switch and an array consuming four ids per
/// element, element-major.

/// Generates the include text the marker is replaced with. The generic form (specDialect=false)
/// declares plain consts — the GL text artifact keeps them verbatim for the runtime to rewrite at
/// injection time; the spec form declares layout(constant_id) constants the Vulkan-family
/// compiles keep as OpSpecConstant. The accessor bodies are identical in both forms: they read
/// the constants when the master switch is on and the uniforms otherwise (the local-variable
/// indirection keeps tint's SPIR-V reader from forming OpSpecConstantOp, which it rejects).

/// Generates the stub pass's pass-through macros: every SPEC_ call expands to the direct uniform
/// read, so the stub compile is semantically today's source and its reflection states the layout
/// both real forms must reproduce.

/// Replaces the marker line with the include text. A source without the marker returns unchanged.

/// The outcome of the text-gate pass.
struct TextGateResult {
  /// The rewritten text (the input unchanged when no gate applied).
  std::string text;
  /// How many gates found and wrapped their function.
  size_t wrapped = 0;
  /// Empty on success; otherwise the reason the text cannot carry the gates (fail the variant).
  std::string error;
};

/// The text dialect a gate pass emits for. The GLSL family's macros carry the TGFX_SPEC_ prefix
/// and the text hangs its gate defines on the #version line; the HLSL SPIRV-Cross emits spells
/// the constants SPIRV_CROSS_CONSTANT_ID_<id>, has no version line, and uses the HLSL scalar and
/// vector types in signatures and stubs.
struct GateDialect {
  /// The runtime-injectable constant macro prefix (SpecContract::ConstantPrefix or
  /// HlslConstantPrefix); the master switch is this prefix plus "0".
  const char* constantPrefix = SpecContract::ConstantPrefix;
  /// Whether the signatures and stubs use the HLSL types (float4) instead of the GLSL ones (vec4).
  bool hlsl = false;
};

/// Collects the `#pragma tgfx_spec_gate <NAME> : <expr>` declarations from one expanded fragment
/// source. The gated function's name is the identifier ending at the first '(' after the pragma
/// line, parsed here because the pragma itself does not survive preprocessing. A pragma not
/// followed by a function definition reports spec.gate-unmatched; a name declared twice keeps its
/// first entry; a comment cannot declare a gate.
std::vector<SpecGateInfo> ScanGatePragmas(const std::string& fragmentSource,
                                          const std::string& shaderName, Diagnostics* diagnostics);

/// Whether this variant's spec table can evaluate the gate's condition: every SPEC_<field>() the
/// expression reads must be a scalar entry, because an ivec4 array element cannot appear in a
/// preprocessor condition. A gate that cannot be evaluated is dropped by the caller, so its body
/// stays in every form — ungated, never cut.
bool GateEvaluatable(const SpecGateInfo& gate, const std::vector<SpecEntry>& entries);

/// Rewrites a preprocessed fragment text so each declared gate becomes a preprocessor
/// conditional: a `#define TGFX_GATE_<name> (<master switch> == 0 || (<cond>))` header line after
/// #version (at the top for a dialect without one), and the gated function's body wrapped as
/// `#if TGFX_GATE_<name> <body> #else <stub> #endif`. The runtime then removes whole code regions
/// by defining the value macros before the driver's preprocessor runs. A gate whose function is
/// absent (the variant pruned the file) is skipped, not an error. The stub keeps the signature and
/// returns the type's zero value, so callers stay valid and the closed branch folds away.
TextGateResult ApplyTextSpecGates(const std::string& preprocessedText,
                                  const std::vector<SpecGateInfo>& gates,
                                  const std::vector<SpecEntry>& entries,
                                  const GateDialect& dialect = {});

/// Replaces every gated function's body with the generated stub, in the 450 source form both
/// real dialects derive from. The build compiles the result once per family, so a stub that
/// cannot satisfy the call sites fails the build instead of surfacing on a device.
std::string StubOutGatedFunctions(const std::string& source,
                                  const std::vector<SpecGateInfo>& gates);

/// Expands every `#pragma tgfx_spec_unroll <fn> <capacity> <guard> (<args>)` line into one
/// guarded call site per slot of the largest declared capacity, so the site count lives in the
/// tool instead of a hand-written block: `if (<guard> > n) { <fn>(n, <args>); }` behind
/// `#if <capacity> > n`, which keeps the smaller profiles a strict subset. Returns the source
/// unchanged when it carries no pragma, and an empty string when a pragma is malformed (the build
/// reports that instead of silently dropping the call sites a GLSL compiler would ignore).

}  // namespace tgfx
