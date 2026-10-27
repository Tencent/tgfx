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

#include <string>
#include <vector>
#include "core/Types.h"

namespace tgfx {

/// Validates that every compiled variant carrying the clip contract (HasClip) declares all
/// ClipContract uniform fields. Returns the error count.
size_t ValidateClipContractFields(const std::vector<VariantData>& variants);

/// Validates the OP_* defines in pointwise_chain_ops.inc against the ChainOp constants that
/// anchor the AOTChainOp enum. Returns the error count.
size_t ValidateChainOpCodes(const std::string& shaderDir);

/// Validates the OP_* defines in pointwise_op.inc against the PointwiseOp constants that anchor
/// the AOTPointwiseOpType enum. Returns the error count.
size_t ValidatePointwiseOpCodes(const std::string& shaderDir);

/// Validates the slot-capacity literals the GLSL kernels carry against the C++ constants they
/// must agree with: the TGFX_SLOT_ARRAY_SUFFIX in perlin_noise.frag / yuv_texture_fill.frag must
/// equal MaxPointwiseSlots, and the CHAIN_SLOT_CAPACITY defines in pointwise_chain.frag must
/// equal the AOTResourceCapacity instruction counts (16 for the plain and tiled-graph kinds, 32
/// for the noise-graph kind). Returns the error count.
size_t ValidateSlotCapacities(const std::string& shaderDir);

/// Validates that the shader sources do not refer to things that no longer exist: every
/// "name.inc/.frag/.vert" token must name a file under shaderDir, and every "XxxShader" token must
/// name a registered shader family (registeredShaders). A reference to a removed file or merged-away
/// family means the surrounding comment or contract describes code that is gone. Placeholders
/// ("<module>_uniforms.inc", "pointwise_*.inc") and external names (SwiftShader) are exempt.
/// Returns the error count.
size_t ValidateShaderSourceReferences(const std::string& shaderDir,
                                      const std::vector<std::string>& registeredShaders);

/// Validates reflection-level invariants across every compiled variant: texture samplers must be
/// named TextureSampler_<N> with N dense from zero, array sizes must be at least one (and exactly
/// one for samplers), and uniform/sampler names must be unique and NUL-free — everything the
/// runtime loader rejects, caught at build time instead. Returns the error count.
size_t ValidateReflectionContracts(const std::vector<VariantData>& variants);

}  // namespace tgfx
