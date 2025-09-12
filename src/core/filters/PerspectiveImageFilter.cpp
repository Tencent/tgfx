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
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

#define FOV_Y_DEGRESS 45.f
#define NORMAL_NEAR_Z 0.25f
#define NORMAL_FAR_Z 1000.f

static constexpr Vec3 eyeTarget = {0.f, 0.f, 0.f};
static constexpr Vec3 eyeUp = {0.f, 1.f, 0.f};

std::shared_ptr<ImageFilter> ImageFilter::Perspective(const PerspectiveInfo& perspective) {
  return std::make_shared<PerspectiveImageFilter>(perspective);
}

PerspectiveImageFilter::PerspectiveImageFilter(const PerspectiveInfo& info) : info(info) {
  auto perspectiveMatrix = Matrix3D::Perspective(FOV_Y_DEGRESS, 1.f, NORMAL_NEAR_Z, NORMAL_FAR_Z);
  const Vec3 eyePosition = {0.f, 0.f, 1.f / tanf(DegreesToRadians(FOV_Y_DEGRESS * 0.5f))};
  const auto viewMatrix = Matrix3D::LookAt(eyePosition, eyeTarget, eyeUp);
  modelMatrix = Matrix3D::MakeRotate({1.f, 0.f, 0.f}, info.xRotation);
  modelMatrix.postRotate({0.f, 1.f, 0.f}, info.yRotation);
  modelMatrix.postRotate({0.f, 0.f, 1.f}, info.zRotation);
  normalTransformMatrix = perspectiveMatrix * viewMatrix * modelMatrix;
}

Rect PerspectiveImageFilter::onFilterBounds(const Rect& srcRect) const {
  //TODO: RichardJieChen
  DEBUG_ASSERT(srcRect.left == 0.f && srcRect.top == 0.f);
  constexpr auto tempRect = Rect::MakeXYWH(-1.f, -1.f, 2.f, 2.f);
  const auto ndcResult = normalTransformMatrix.mapRect(tempRect);
  const auto normalizedResult =
      Rect::MakeXYWH((ndcResult.left + 1.f) * 0.5f, (ndcResult.top + 1.f) * 0.5f,
                     ndcResult.width() * 0.5f, ndcResult.height() * 0.5f);
  const auto result = Rect::MakeXYWH(
      normalizedResult.left * srcRect.width(), normalizedResult.top * srcRect.height(),
      normalizedResult.width() * srcRect.width(), normalizedResult.height() * srcRect.height());
  return result;
}

std::shared_ptr<TextureProxy> PerspectiveImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& renderBounds, const TPArgs& args) const {
  const auto sourceW = static_cast<float>(source->width());
  const auto sourceH = static_cast<float>(source->height());
  const auto srcRect = Rect::MakeXYWH(-sourceW * 0.5f, -sourceH * 0.5f, sourceW, sourceH);

  const auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(renderBounds.width()), static_cast<int>(renderBounds.height()),
      source->isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft, args.backingFit);

  const auto sourceTextureProxy = source->lockTextureProxy(args);

  const float eyePositionZ = sourceH * 0.5f / tanf(DegreesToRadians(FOV_Y_DEGRESS * 0.5f));
  const Vec3 eyePosition = {0.f, 0.f, eyePositionZ};
  const auto viewMatrix = Matrix3D::LookAt(eyePosition, eyeTarget, eyeUp);
  const float nearZ = std::min(NORMAL_NEAR_Z, eyePositionZ * 0.1f);
  const float farZ = std::max(NORMAL_FAR_Z, eyePositionZ * 10.f);
  const auto perspectiveMatrix = Matrix3D::Perspective(
      FOV_Y_DEGRESS, renderBounds.width() / renderBounds.height(), nearZ, farZ);
  const auto transformMatrix = perspectiveMatrix * viewMatrix * modelMatrix;

  const auto drawingManager = args.context->drawingManager();
  drawingManager->addRectPerspectiveRenderTask(srcRect, AAType::Coverage, renderTarget,
                                               sourceTextureProxy, transformMatrix);
  return renderTarget->asTextureProxy();
}

PlacementPtr<FragmentProcessor> PerspectiveImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

}  // namespace tgfx