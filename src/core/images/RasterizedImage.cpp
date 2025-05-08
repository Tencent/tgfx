/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "RasterizedImage.h"
#include "core/images/SubsetImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {
static int GetSize(int size, float scale) {
  return static_cast<int>(roundf(static_cast<float>(size) * scale));
}

std::shared_ptr<Image> RasterizedImage::MakeFrom(std::shared_ptr<Image> source,
                                                 float rasterizationScale,
                                                 const SamplingOptions& sampling) {
  if (source == nullptr || rasterizationScale <= 0) {
    return nullptr;
  }
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto width = GetSize(sourceWidth, rasterizationScale);
  auto height = GetSize(sourceHeight, rasterizationScale);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (rasterizationScale != 1.0f && sampling.mipmapMode != MipmapMode::None &&
      !source->hasMipmaps()) {
    // Generate mipmaps for the source image if required by the sampling options.
    auto newSource = source->makeMipmapped(true);
    if (newSource != nullptr) {
      source = std::move(newSource);
    }
  }
  auto result = std::shared_ptr<RasterizedImage>(
      new RasterizedImage(UniqueKey::Make(), std::move(source), rasterizationScale, sampling));
  result->weakThis = result;
  return result;
}

RasterizedImage::RasterizedImage(UniqueKey uniqueKey, std::shared_ptr<Image> source,
                                 float rasterizationScale, const SamplingOptions& sampling)
    : OffscreenImage(std::move(uniqueKey)), source(std::move(source)),
      rasterizationScale(rasterizationScale), sampling(sampling) {
}

int RasterizedImage::width() const {
  return GetSize(source->width(), rasterizationScale);
}

int RasterizedImage::height() const {
  return GetSize(source->height(), rasterizationScale);
}

std::shared_ptr<Image> RasterizedImage::makeRasterized(float newScale,
                                                       const SamplingOptions& sampling) const {
  return MakeFrom(source, rasterizationScale * newScale, sampling);
}

std::shared_ptr<Image> RasterizedImage::onMakeDecoded(Context* context, bool) const {
  // There is no need to pass tryHardware (disabled) to the source image, as our texture proxy is
  // not locked from the source image.
  auto newSource = source->onMakeDecoded(context);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<RasterizedImage>(
      new RasterizedImage(uniqueKey, std::move(newSource), rasterizationScale, sampling));
  newImage->weakThis = newImage;
  return newImage;
}

bool RasterizedImage::onDraw(std::shared_ptr<RenderTargetProxy> renderTarget,
                             uint32_t renderFlags) const {
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto scaledWidth = GetSize(sourceWidth, rasterizationScale);
  auto scaledHeight = GetSize(sourceHeight, rasterizationScale);
  auto uvScaleX = static_cast<float>(sourceWidth) / static_cast<float>(scaledWidth);
  auto uvScaleY = static_cast<float>(sourceHeight) / static_cast<float>(scaledHeight);
  Matrix uvMatrix = Matrix::MakeScale(uvScaleX, uvScaleY);
  auto drawRect = Rect::MakeWH(width(), height());
  FPArgs args(renderTarget->getContext(), renderFlags, drawRect);
  auto processor = FragmentProcessor::Make(source, args, sampling, &uvMatrix);
  auto drawingManager = renderTarget->getContext()->drawingManager();
  return drawingManager->fillRTWithFP(std::move(renderTarget), std::move(processor), renderFlags);
}
}  // namespace tgfx