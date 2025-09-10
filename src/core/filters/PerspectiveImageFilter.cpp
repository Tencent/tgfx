/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "PerspectiveImageFilter.h"
#include "core/Matrix3D.h"
#include "core/utils/MathExtra.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

#define FOV_Y_DEGRESS 45.0f
#define NEAR_Z 1.0f
#define FAR_Z 1000.0f

std::shared_ptr<ImageFilter> ImageFilter::Perspective(const PerspectiveInfo& perspective) {
  return std::make_shared<PerspectiveImageFilter>(perspective);
}

PerspectiveImageFilter::PerspectiveImageFilter(const PerspectiveInfo& info) : info(info) {
}

Rect PerspectiveImageFilter::onFilterBounds(const Rect& srcRect) const {
  const auto perspectiveMatrix =
      Matrix3D::Perspective(FOV_Y_DEGRESS, srcRect.width() / srcRect.height(), NEAR_Z, FAR_Z);
  const Vec3 eyePosition = {0, 0, 1.0f / tanf(DegreesToRadians(FOV_Y_DEGRESS) / 2)};
  constexpr Vec3 eyeTarget = {0, 0, 0};
  constexpr Vec3 eyeUp = {0, 1, 0};
  const auto viewMatrix = Matrix3D::LookAt(eyePosition, eyeTarget, eyeUp);
  auto modelMatrix = Matrix3D::MakeRotate({1, 0, 0}, info.xRotation);
  modelMatrix.postRotate({0, 1, 0}, info.yRotation);
  modelMatrix.postRotate({0, 0, 1}, info.zRotation);
  const auto transformMatrix = perspectiveMatrix * viewMatrix * modelMatrix;
  const auto result = transformMatrix.mapRect(srcRect);
  return result;
}

std::shared_ptr<TextureProxy> PerspectiveImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& renderBounds, const TPArgs& args) const {
  (void)source;
  (void)renderBounds;
  (void)args;
  //TODO: RicharrdChen
  return nullptr;
}

PlacementPtr<FragmentProcessor> PerspectiveImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

}  // namespace tgfx