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

namespace tgfx {

/**
 * Describes a single shader variant of a processor. A variant is a distinct shader source file
 * that would be produced for one specific combination of the processor's compile-time flags
 * (macros, enums, etc.). Used by offline tooling (e.g. UE RHI adaptation) to enumerate and
 * pre-compile every shader source the processor may need at runtime.
 *
 * Variants are emitted by each processor via a static EnumerateVariants() method. A variant is
 * identified by a stable index within its processor; the name and preamble fields are purely
 * informational. The runtimeKeyHash is a deterministic digest of the variant's preamble content
 * that tooling can cross-check against runtime programKey bits to verify uniqueness.
 */
struct ShaderVariant {
  /**
   * Zero-based index within the enumerating processor. Stable across runs: index i always refers
   * to the same variant. Useful as a compact primary key in external shader caches.
   */
  int index = 0;

  /**
   * Human-readable identifier, e.g. "PorterDuffXP[mode=Overlay,dst=1]". Not parsed; for logging
   * and offline tooling diagnostics only.
   */
  std::string name;

  /**
   * GLSL preprocessor preamble (a concatenation of `#define ...\n` lines). Can be prepended to
   * the shader source to produce the exact compiled output corresponding to this variant.
   * Identical to what Processor::onBuildShaderMacros + ShaderMacroSet::toPreamble would emit at
   * runtime for the same configuration.
   */
  std::string preamble;

  /**
   * Stable 64-bit hash of the preamble string. Two variants with the same preamble hash are
   * byte-identical shaders; uniqueness across a processor's variant set is enforced by tests.
   */
  uint64_t runtimeKeyHash = 0;
};

}  // namespace tgfx
