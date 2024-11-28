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

#include "ScaleImage.h"
#include "core/images/SubsetImage.h"
#include "gpu/OpContext.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {
static int GetSize(int size, float scale) {
  return static_cast<int>(roundf(static_cast<float>(size) * scale));
}

std::shared_ptr<Image> ScaleImage::MakeFrom(std::shared_ptr<Image> source, float scale,
                                            const SamplingOptions& sampling) {
  TRACE_EVENT;
  if (source == nullptr) {
    return nullptr;
  }
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto width = GetSize(sourceWidth, scale);
  auto height = GetSize(sourceHeight, scale);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (width == sourceWidth && height == sourceHeight) {
    return source;
  }
  auto result = std::shared_ptr<ScaleImage>(
      new ScaleImage(UniqueKey::Make(), std::move(source), scale, sampling));
  result->weakThis = result;
  return result;
}

ScaleImage::ScaleImage(UniqueKey uniqueKey, std::shared_ptr<Image> source, float scale,
                       const SamplingOptions& sampling)
    : OffscreenImage(std::move(uniqueKey)), source(std::move(source)), scale(scale),
      sampling((sampling)) {
}

int ScaleImage::width() const {
  return GetSize(source->width(), scale);
}

int ScaleImage::height() const {
  return GetSize(source->height(), scale);
}

std::shared_ptr<Image> ScaleImage::makeScaled(float newScale,
                                              const SamplingOptions& sampling) const {
  TRACE_EVENT;
  return MakeFrom(source, scale * newScale, sampling);
}

std::shared_ptr<Image> ScaleImage::onMakeDecoded(Context* context, bool) const {
  TRACE_EVENT;
  // There is no need to pass tryHardware (disabled) to the source image, as our texture proxy is
  // not locked from the source image.
  auto newSource = source->onMakeDecoded(context);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage =
      std::shared_ptr<ScaleImage>(new ScaleImage(uniqueKey, std::move(newSource), scale, sampling));
  newImage->weakThis = newImage;
  return newImage;
}

bool ScaleImage::onDraw(std::shared_ptr<RenderTargetProxy> renderTarget,
                        uint32_t renderFlags) const {
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto scaledWidth = GetSize(sourceWidth, scale);
  auto scaledHeight = GetSize(sourceHeight, scale);
  auto uvScaleX = static_cast<float>(sourceWidth) / static_cast<float>(scaledWidth);
  auto uvScaleY = static_cast<float>(sourceHeight) / static_cast<float>(scaledHeight);
  Matrix uvMatrix = Matrix::MakeScale(uvScaleX, uvScaleY);
  auto drawRect = Rect::MakeWH(width(), height());
  FPArgs args(renderTarget->getContext(), renderFlags, drawRect, Matrix::I());
  auto processor = FragmentProcessor::Make(source, args, sampling, &uvMatrix);
  if (processor == nullptr) {
    return false;
  }
  OpContext opContext(renderTarget, args.renderFlags);
  opContext.fillWithFP(std::move(processor), Matrix::I(), true);
  return true;
}
}  // namespace tgfx