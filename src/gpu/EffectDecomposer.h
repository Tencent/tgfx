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

#include <memory>
#include <vector>
#include "gpu/DrawingManager.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

class Context;
class RenderTargetProxy;

/**
 * EffectDecomposer attempts to split a chain of multiple color fragment processors into
 * multiple render passes, enabling the precompiled shader system to handle complex effects
 * that would otherwise require runtime GLSL generation.
 *
 * When OpsCompositor builds a DrawOp with multiple color FPs (e.g., shader FP + color filter FP),
 * the combined pipeline may not match any single precompiled shader. EffectDecomposer renders
 * earlier FPs to intermediate textures (via fillRTWithFP), then replaces the entire chain with
 * a single TextureEffect sampling the intermediate result composed with the final FP.
 */
class EffectDecomposer {
 public:
  /**
   * Attempts to decompose multiple color FPs into a multi-pass pipeline. On success, renders
   * all but the last FP to intermediate textures and returns a single replacement FP that the
   * caller should use in place of the original chain.
   *
   * @param context        The GPU context.
   * @param drawingManager The drawing manager for submitting intermediate render passes.
   * @param width          Width of the render target (for intermediate texture sizing).
   * @param height         Height of the render target (for intermediate texture sizing).
   * @param processors     The color FP chain to decompose. Must contain >= 2 processors.
   *                       Ownership is taken; on failure the contents are left in moved-from state.
   * @param allocator      Block allocator for new processor construction.
   * @return A replacement FP on success, nullptr on failure (callers should fallback).
   */
  static PlacementPtr<FragmentProcessor> TryDecompose(
      Context* context, DrawingManager* drawingManager, int width, int height,
      std::vector<PlacementPtr<FragmentProcessor>>& processors, BlockAllocator* allocator);
};

}  // namespace tgfx
