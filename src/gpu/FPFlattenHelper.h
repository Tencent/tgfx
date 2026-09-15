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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

/**
 * Renders a complex FragmentProcessor into an offscreen texture and returns a simple TextureEffect
 * that samples from it. The FP's coordTransform expects to receive coordinates in the range
 * [drawRect.x..right, drawRect.y..bottom], so a coordOffset is applied during offscreen rendering
 * to ensure the FP sees the correct coordinate space. apronRadius expands the captured area on every
 * side; pass the value the materialization policy produced for this consumer. Returns nullptr on
 * failure.
 */
PlacementPtr<FragmentProcessor> FlattenToTexture(const FPArgs& args,
                                                 PlacementPtr<FragmentProcessor> fp,
                                                 float apronRadius = 0.0f);

/**
 * Ensures the given FragmentProcessor is simple for use as a blend child at the specified position.
 * If it's already simple for that position, returns it unchanged; otherwise flattens it to a
 * texture. Returns nullptr on failure.
 *
 * Two independent reasons trigger flattening, both decided by AOTMaterializationPolicy:
 *  - Correctness: the child is too complex to be a valid XfermodeFragmentProcessor child. This
 *    always flattens on the default route. Under TGFX_AOT_DISABLE (the pure runtime route)
 *    the child stays inline, matching main's recursive child emission, so runtime-only
 *    comparisons exercise the original tree instead of a shared rewrite.
 *  - AOT matchability: the child is valid inline but its permutation has no precompiled artifact
 *    (e.g. a TiledTextureEffect src). This flattens only when the decomposition route is enabled and
 *    the draw is not already inside a nested rasterization, so the default path is byte-for-byte
 *    identical to before and no baseline shifts.
 */
/**
 * True when TGFX_AOT_LEGACY_BLEND_MATERIALIZATION is set: BlendShader materializes its children
 * at construction time (the pre-P4 behavior). The default keeps the original tree and lets the
 * in-plan materialization retry in OpsCompositor decide, so a failed retry falls back with the
 * untouched original processors.
 */
bool BlendChildMaterializationIsLegacy();

PlacementPtr<FragmentProcessor> EnsureSimpleBlendChild(const FPArgs& args,
                                                       PlacementPtr<FragmentProcessor> fp,
                                                       size_t childIndex = 0);

}  // namespace tgfx
