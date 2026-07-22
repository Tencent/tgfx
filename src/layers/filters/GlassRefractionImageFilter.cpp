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

#include "GlassRefractionImageFilter.h"
#include "core/images/TextureImage.h"
#include "gpu/TPArgs.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"

namespace tgfx {

GlassRefractionImageFilter::GlassRefractionImageFilter(const GlassRefractionParams& params,
                                                       std::shared_ptr<Image> fineMask,
                                                       std::shared_ptr<Image> coarseMask)
    : params(params), fineMask(std::move(fineMask)), coarseMask(std::move(coarseMask)) {
}

static std::shared_ptr<TextureProxy> MakeTextureProxy(Context* context,
                                                      const std::shared_ptr<Image>& image) {
  if (image == nullptr || context == nullptr) {
    return nullptr;
  }
  auto textureImage = image->makeTextureImage(context);
  if (textureImage == nullptr) {
    return nullptr;
  }
  auto textureImageImpl = std::dynamic_pointer_cast<TextureImage>(textureImage);
  return textureImageImpl ? textureImageImpl->getTextureProxy() : nullptr;
}

PlacementPtr<FragmentProcessor> GlassRefractionImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& /*sampling*/,
    SrcRectConstraint /*constraint*/, const Matrix* uvMatrix) const {
  if (source == nullptr || args.context == nullptr) {
    return nullptr;
  }

  auto sourceProxy = MakeTextureProxy(args.context, source);
  if (sourceProxy == nullptr) {
    return nullptr;
  }

  std::shared_ptr<TextureProxy> fineMaskProxy = nullptr;
  std::shared_ptr<TextureProxy> coarseMaskProxy = nullptr;
  if (fineMask != nullptr) {
    fineMaskProxy = MakeTextureProxy(args.context, fineMask);
    if (fineMaskProxy == nullptr) {
      return nullptr;
    }
  }
  if (coarseMask != nullptr) {
    coarseMaskProxy = MakeTextureProxy(args.context, coarseMask);
    if (coarseMaskProxy == nullptr) {
      return nullptr;
    }
  }

  auto coordMatrix = uvMatrix != nullptr ? *uvMatrix : Matrix::I();
  auto localParams = params;
  localParams.renderOffsetX = 0.0f;
  localParams.renderOffsetY = 0.0f;
  return GlassRefractionFragmentProcessor::Make(
      args.context->drawingAllocator(), std::move(sourceProxy), std::move(fineMaskProxy),
      std::move(coarseMaskProxy), localParams, coordMatrix);
}

}  // namespace tgfx
