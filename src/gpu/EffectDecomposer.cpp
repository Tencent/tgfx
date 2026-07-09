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

#include "EffectDecomposer.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> EffectDecomposer::TryDecompose(
    Context* context, DrawingManager* drawingManager, int width, int height,
    std::vector<PlacementPtr<FragmentProcessor>>& processors, BlockAllocator* allocator) {
  if (processors.size() < 2 || width <= 0 || height <= 0) {
    return nullptr;
  }

  // Strategy: render all FPs except the last one sequentially into intermediate textures.
  // Each pass takes the previous texture as input (via TextureEffect) and applies the next FP.
  // The final FP is returned as the replacement for the original chain.
  //
  // Example: [ShaderFP, ColorFilterFP, MaskFP]
  //   Pass 1: render ShaderFP → intermediateTexture1
  //   Pass 2: render TextureEffect(intermediateTexture1) composed with ColorFilterFP → intermediateTexture2
  //   Return: ComposeFragmentProcessor(TextureEffect(intermediateTexture2), MaskFP)

  std::shared_ptr<TextureProxy> intermediateTexture = nullptr;

  for (size_t i = 0; i < processors.size() - 1; ++i) {
    auto intermediateRT = RenderTargetProxy::Make(context, width, height, false, 1, false,
                                                  ImageOrigin::TopLeft, BackingFit::Approx);
    if (!intermediateRT) {
      return nullptr;
    }

    PlacementPtr<FragmentProcessor> passProcessor;
    if (i == 0) {
      // First pass: render the first FP directly to an intermediate texture.
      passProcessor = std::move(processors[i]);
    } else {
      // Subsequent passes: sample the previous intermediate texture and compose with current FP.
      auto texFP = TextureEffect::Make(allocator, intermediateTexture);
      if (!texFP) {
        return nullptr;
      }
      passProcessor =
          ComposeFragmentProcessor::Make(allocator, std::move(texFP), std::move(processors[i]));
    }

    if (!passProcessor) {
      return nullptr;
    }

    if (!drawingManager->fillRTWithFP(intermediateRT, std::move(passProcessor), 0)) {
      return nullptr;
    }

    intermediateTexture = intermediateRT->asTextureProxy();
    if (!intermediateTexture) {
      return nullptr;
    }
  }

  // Create the final replacement FP: sample intermediate texture composed with the last FP.
  auto texFP = TextureEffect::Make(allocator, intermediateTexture);
  if (!texFP) {
    return nullptr;
  }

  auto& lastFP = processors.back();
  if (!lastFP) {
    return texFP;
  }

  return ComposeFragmentProcessor::Make(allocator, std::move(texFP), std::move(lastFP));
}

}  // namespace tgfx
