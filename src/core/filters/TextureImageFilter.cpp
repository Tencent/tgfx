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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "core/filters/TextureImageFilter.h"
#include <cmath>
#include "core/images/TextureImage.h"
#include "core/utils/TextureGrainNoise.h"
#include "gpu/processors/TextureGrainFragmentProcessor.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

namespace {

float MaximumDisplacement(const TextureGrainParams& params) {
  return std::abs(params.radius) * params.pixelsPerLocalUnit * 2.5f;
}

std::shared_ptr<TextureProxy> MakeTextureProxy(Context* context,
                                               const std::shared_ptr<Image>& image) {
  if (image == nullptr || context == nullptr) {
    return nullptr;
  }
  auto textureImage = image->makeTextureImage(context);
  if (textureImage == nullptr) {
    return nullptr;
  }
  auto textureImageImpl = std::static_pointer_cast<TextureImage>(textureImage);
  return textureImageImpl->getTextureProxy();
}

}  // namespace

TextureGrainParams MakeTextureGrainParams(float sizeX, float sizeY, float radius,
                                          const Point& nodeCenter, uint32_t seed,
                                          const Point& sourceOrigin, float pixelsPerLocalUnit,
                                          bool clipToShape) {
  TextureGrainParams params = {};
  auto frequencyX = std::isfinite(sizeX) && sizeX > 0.0f ? 1.0f / sizeX : 0.0f;
  auto frequencyY = std::isfinite(sizeY) && sizeY > 0.0f ? 1.0f / sizeY : 0.0f;
  params.frequencyX = std::isfinite(frequencyX) ? frequencyX : 0.0f;
  params.frequencyY = std::isfinite(frequencyY) ? frequencyY : 0.0f;
  params.radius = radius;
  params.seed = seed;
  params.nodeCenter = nodeCenter;
  params.sourceOrigin = sourceOrigin;
  params.pixelsPerLocalUnit = pixelsPerLocalUnit;
  params.clipToShape = clipToShape;
  return params;
}

TextureImageFilter::TextureImageFilter(const TextureGrainParams& params) : params(params) {
  if (!std::isfinite(this->params.pixelsPerLocalUnit) || this->params.pixelsPerLocalUnit <= 0.0f) {
    this->params.pixelsPerLocalUnit = 1.0f;
  }
}

Rect TextureImageFilter::onFilterBounds(const Rect& rect, MapDirection) const {
  auto maximumDisplacement = MaximumDisplacement(params);
  if (maximumDisplacement <= 0.0f) {
    return rect;
  }
  auto outset = maximumDisplacement + 2.0f;
  return rect.makeOutset(outset, outset);
}

PlacementPtr<FragmentProcessor> TextureImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  if (source == nullptr || args.context == nullptr) {
    return nullptr;
  }
  if (params.radius == 0.0f) {
    return FragmentProcessor::Make(source, args, sampling, constraint, uvMatrix);
  }

  auto sourceProxy = MakeTextureProxy(args.context, source);
  if (sourceProxy == nullptr) {
    return nullptr;
  }
  const auto& table = TextureGrainGradientTable();
  auto gradientView =
      TextureView::MakeRGBA(args.context, TextureGrainTableSize, TextureGrainTableSize,
                            table.data(), static_cast<size_t>(TextureGrainTableSize) * 4);
  if (gradientView == nullptr) {
    return nullptr;
  }

  auto coordMatrix = uvMatrix != nullptr ? *uvMatrix : Matrix::I();
  auto useHardwareDecal = args.context->gpu()->features()->clampToBorder;
  return TextureGrainFragmentProcessor::Make(args.context->drawingAllocator(),
                                             std::move(sourceProxy), std::move(gradientView),
                                             params, sampling, useHardwareDecal, coordMatrix);
}

}  // namespace tgfx
