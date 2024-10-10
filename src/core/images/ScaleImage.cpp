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
#include "gpu/ops/DrawOp.h"

namespace tgfx {
int ScaleImage::GetSize(int size, float scale) {
  return static_cast<int>(roundf(static_cast<float>(size) * scale));
}

std::shared_ptr<Image> ScaleImage::MakeFrom(std::shared_ptr<Image> source, const Point& scale) {
  if (source == nullptr) {
    return nullptr;
  }
  auto width = GetSize(source->width(), scale.x);
  auto height = GetSize(source->height(), scale.y);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto result = std::shared_ptr<ScaleImage>(new ScaleImage(std::move(source), scale));
  result->weakThis = result;
  return result;
}

ScaleImage::ScaleImage(std::shared_ptr<Image> source, const Point& scale)
    : TransformImage(std::move(source)), scale(scale) {
}

int ScaleImage::width() const {
  return GetSize(source->width(), scale.x);
}

int ScaleImage::height() const {
  return GetSize(source->height(), scale.y);
}

std::shared_ptr<Image> ScaleImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return MakeFrom(std::move(newSource), scale);
}

std::shared_ptr<Image> ScaleImage::onMakeScaled(float newScaleX, float newScaleY) const {
  return MakeFrom(source, Point::Make(scale.x * newScaleX, scale.y * newScaleY));
}

std::unique_ptr<FragmentProcessor> ScaleImage::asFragmentProcessor(const FPArgs& args,
                                                                   TileMode tileModeX,
                                                                   TileMode tileModeY,
                                                                   const SamplingOptions& sampling,
                                                                   const Matrix* uvMatrix) const {
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto scaledWidth = GetSize(sourceWidth, scale.x);
  auto scaledHeight = GetSize(sourceHeight, scale.y);
  auto uvScaleX = static_cast<float>(sourceWidth) / static_cast<float>(scaledWidth);
  auto uvScaleY = static_cast<float>(sourceHeight) / static_cast<float>(scaledHeight);
  Matrix matrix = Matrix::MakeScale(uvScaleX, uvScaleY);
  if (uvMatrix) {
    matrix.preConcat(*uvMatrix);
  }
  return FragmentProcessor::Make(source, args, tileModeX, tileModeY, sampling, &matrix);
}
}  // namespace tgfx