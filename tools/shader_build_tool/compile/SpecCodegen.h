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
};

/// Scans one family's expanded fragment template. Collects the include marker and the
/// SPEC_<name>() call sites, and reports spec.raw-read: a spec'd uniform name appearing outside
/// its UBO declaration line and outside the SPEC_ call itself reads the uniform directly, so the
/// read escapes the folding a specialized compile performs.
SpecUsage ScanSpecUsage(const std::string& fragmentSource, const std::string& shaderName,
                        Diagnostics* diagnostics);

/// Maps the scanned fields onto one variant's reflected uniform block. Every name must exist as
/// an int or ivec4 member (reported as spec.unknown-field otherwise); ids are assigned
/// deterministically in scan order, 0 being the master switch and an array consuming four ids per
/// element, element-major.
std::vector<SpecEntry> MakeSpecEntries(const SpecUsage& usage,
                                       const StageReflectionData& reflection,
                                       const std::string& shaderName, uint32_t fragIndex,
                                       Diagnostics* diagnostics);

/// Generates the include text the marker is replaced with. The generic form (specDialect=false)
/// declares plain consts — the GL text artifact keeps them verbatim for the runtime to rewrite at
/// injection time; the spec form declares layout(constant_id) constants the Vulkan-family
/// compiles keep as OpSpecConstant. The accessor bodies are identical in both forms: they read
/// the constants when the master switch is on and the uniforms otherwise (the local-variable
/// indirection keeps tint's SPIR-V reader from forming OpSpecConstantOp, which it rejects).
std::string GenerateSpecInclude(const std::vector<SpecEntry>& entries, bool specDialect);

/// Generates the stub pass's pass-through macros: every SPEC_ call expands to the direct uniform
/// read, so the stub compile is semantically today's source and its reflection states the layout
/// both real forms must reproduce.
std::string GenerateSpecStubInclude(const SpecUsage& usage);

/// Replaces the marker line with the include text. A source without the marker returns unchanged.
std::string ReplaceSpecMarker(const std::string& source, const std::string& includeText);

/// Expands every `#pragma tgfx_spec_unroll <fn> <capacity> <guard> (<args>)` line into one
/// guarded call site per slot of the largest declared capacity, so the site count lives in the
/// tool instead of a hand-written block: `if (<guard> > n) { <fn>(n, <args>); }` behind
/// `#if <capacity> > n`, which keeps the smaller profiles a strict subset. Returns the source
/// unchanged when it carries no pragma, and an empty string when a pragma is malformed (the build
/// reports that instead of silently dropping the call sites a GLSL compiler would ignore).
std::string ExpandUnrollPragmas(const std::string& source);

}  // namespace tgfx
