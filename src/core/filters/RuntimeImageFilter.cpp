/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "RuntimeImageFilter.h"
#include "core/images/PixelImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/tasks/RuntimeDrawTask.h"

namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::Runtime(std::shared_ptr<RuntimeEffect> effect) {
  if (effect == nullptr) {
    return nullptr;
  }
  return std::make_shared<RuntimeImageFilter>(effect);
}

Rect RuntimeImageFilter::onFilterBounds(const Rect& rect, MapDirection mapDirection) const {
  return effect->filterBounds(rect, mapDirection);
}

std::shared_ptr<TextureProxy> RuntimeImageFilter::lockTextureProxy(std::shared_ptr<Image> source,
                                                                   const Rect& renderBounds,
                                                                   const TPArgs& args) const {
  auto renderTarget = RenderTargetProxy::Make(args.context, static_cast<int>(renderBounds.width()),
                                              static_cast<int>(renderBounds.height()),
                                              source->isAlphaOnly(), 1, args.mipmapped);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  std::vector<RuntimeInputTexture> inputTextures = {};
  inputTextures.reserve(1 + effect->extraInputs.size());
  // Request a texture proxy from the source image without mipmaps to save memory.
  // It may be ignored if the source image has preset mipmaps.
  TPArgs tpArgs(args.context, args.renderFlags, false, 1.0f, BackingFit::Exact);
  auto textureProxy = source->lockTextureProxy(tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  inputTextures.push_back({textureProxy, source->colorSpace()});
  for (size_t i = 0; i < effect->extraInputs.size(); i++) {
    const auto& input = effect->extraInputs[i];
    if (input == nullptr) {
      LOGE("RuntimeImageFilter::lockTextureProxy() extraInput %d is nullptr", i);
      return nullptr;
    }
    textureProxy = input->lockTextureProxy(tpArgs);
    if (textureProxy == nullptr) {
      return nullptr;
    }
    inputTextures.push_back({textureProxy, input->colorSpace()});
  }
  auto offset = Point::Make(-renderBounds.x(), -renderBounds.y());
  auto drawingManager = args.context->drawingManager();
  drawingManager->addRuntimeDrawTask(renderTarget, std::move(inputTextures), effect, offset);
  return renderTarget->asTextureProxy();
}

PlacementPtr<FragmentProcessor> RuntimeImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}
}  // namespace tgfx
