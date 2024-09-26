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
#include "gpu/ops/DrawOp.h"

namespace tgfx {

std::shared_ptr<Image> ScaleImage::MakeFrom(std::shared_ptr<Image> source, float scaleX,
                                            float scaleY) {
  if (scaleX <= 0 || scaleY <= 0 || source == nullptr) {
    return nullptr;
  }
  if (scaleX == 1.0f && scaleY == 1.0f) {
    return source;
  }
  auto result = std::shared_ptr<ScaleImage>(new ScaleImage(std::move(source), scaleX, scaleY));
  result->weakThis = result;
  return result;
}

static int GetScaledSize(int size, float scale) {
  return static_cast<int>(roundf(static_cast<float>(size) * scale));
}

int ScaleImage::width() const {
  return GetScaledSize(source->width(), scaleX);
}

int ScaleImage::height() const {
  return GetScaledSize(source->height(), scaleY);
}

std::shared_ptr<Image> ScaleImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return MakeFrom(std::move(newSource), scaleX, scaleY);
}

std::shared_ptr<Image> ScaleImage::onMakeScale(float newScaleX, float newScaleY) const {
  return MakeFrom(source, scaleX * newScaleX, scaleY * newScaleY);
}

std::unique_ptr<FragmentProcessor> ScaleImage::asFragmentProcessor(const FPArgs& args,
                                                                   TileMode tileModeX,
                                                                   TileMode tileModeY,
                                                                   const SamplingOptions& sampling,
                                                                   const Matrix* uvMatrix) const {
  Matrix matrix = Matrix::MakeScale(1 / scaleX, 1 / scaleY);
  if (uvMatrix != nullptr) {
    matrix.preConcat(*uvMatrix);
  }
  return FragmentProcessor::Make(source, args, tileModeX, tileModeY, sampling,
                                 std::addressof(matrix));
}

ScaleImage::ScaleImage(std::shared_ptr<Image> source, float scaleX, float scaleY)
    : TransformImage(std::move(source)), scaleX(scaleX), scaleY(scaleY) {
}

}  // namespace tgfx