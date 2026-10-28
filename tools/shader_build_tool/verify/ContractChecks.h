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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "core/Diagnostics.h"
#include "core/Types.h"

namespace tgfx {

// Every check reports each violation as a Diagnostic (core/Diagnostics.h) with its stable code
// and returns the number it reported; the build gates on the sum. The checks never print — the
// cli renders the records — so a test asserts the code and the subject fields instead of
// capturing stderr.

/// Validates that every compiled variant carrying the clip contract (HasClip) declares all
/// ClipContract uniform fields. Returns the error count.
size_t ValidateClipContractFields(const std::vector<VariantData>& variants,
                                  Diagnostics* diagnostics);

/// Validates the OP_* defines in operator/op_codes.inc against the Op constants that anchor the
/// AOTChainOp enum. Returns the error count.
size_t ValidateOpCodes(const std::string& shaderDir, Diagnostics* diagnostics);

/// Validates that every OP_* reference in the single-slot interpreter (pointwise_op.inc) is an
/// operator the C++ side can emit into a slot (the AOTPointwiseOpType values, anchored to the Op
/// constants). Returns the error count.
size_t ValidateSlotOpReferences(const std::string& shaderDir, Diagnostics* diagnostics);

/// Validates that operator/op_codes.inc is the only shader file defining an OP_* macro. Returns
/// the error count.
size_t ValidateOpTableExclusivity(const std::string& shaderDir, Diagnostics* diagnostics);

/// Validates the slot-capacity literals the GLSL kernels carry against the C++ constants they
/// must agree with: the TGFX_SLOT_ARRAY_SUFFIX in perlin_noise.frag / yuv_texture_fill.frag must
/// equal MaxPointwiseSlots, and the CHAIN_SLOT_CAPACITY defines in pointwise_chain.frag must
/// equal the AOTResourceCapacity instruction counts (16 for the plain and tiled-graph kinds, 32
/// for the noise-graph kind). Returns the error count.
size_t ValidateSlotCapacities(const std::string& shaderDir, Diagnostics* diagnostics);

/// Validates that the shader sources do not refer to things that no longer exist: every
/// "name.inc/.frag/.vert" token must name a file under shaderDir, and every "XxxShader" token must
/// name a registered shader family (registeredShaders). A reference to a removed file or merged-away
/// family means the surrounding comment or contract describes code that is gone. Placeholders
/// ("<module>_uniforms.inc", "pointwise_*.inc") and external names (SwiftShader) are exempt.
/// Returns the error count.
size_t ValidateShaderSourceReferences(const std::string& shaderDir,
                                      const std::vector<std::string>& registeredShaders,
                                      Diagnostics* diagnostics);

/// Validates reflection-level invariants across every compiled variant: texture samplers must be
/// named TextureSampler_<N> with N dense from zero, array sizes must be at least one (and exactly
/// one for samplers), and uniform/sampler names must be unique and NUL-free — everything the
/// runtime loader rejects, caught at build time instead. Returns the error count.
size_t ValidateReflectionContracts(const std::vector<VariantData>& variants,
                                   Diagnostics* diagnostics);

/// Validates the expansion policy over the fragment stages: the instructions a function adds by
/// being inlined more than once (its own instructions times its copies beyond the first,
/// DuplicatedInstructions in core/Types.h) must stay below MaxDuplicatedInstructions, with no
/// exceptions. A function with several call sites is the pattern that multiplied the pointwise
/// kernels' compile time (per-leaf tiled fetches, per-mode HSL blend helpers, per-channel transfer
/// functions, the XferProcessor blend next to the loop's blend); funnelling the call sites into
/// one is what removes the copies. Reports each violation and each unanalyzable module as a
/// diagnostic and returns the count.
size_t ValidateInlineExpansion(const std::vector<InlineExpansionRecord>& records,
                               Diagnostics* diagnostics);

}  // namespace tgfx
