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
#include "gpu/tasks/RectPerspectiveRenderTask.h"

namespace tgfx {

// The field of view (in degrees) for the standard perspective projection model.
static constexpr float StandardFovYDegress = 45.f;
// The maximum position of the near plane on the Z axis for the standard perspective projection model.
static constexpr float StandardMaxNearZ = 0.25f;
// The minimum position of the far plane on the Z axis for the standard perspective projection model.
static constexpr float StandardMinFarZ = 1000.f;
// The target position of the camera for the standard perspective projection model, in pixels.
static constexpr Vec3 StandardEyeCenter = {0.f, 0.f, 0.f};
// The up direction unit vector for the camera in the standard perspective projection model.
static constexpr Vec3 StandardEyeUp = {0.f, 1.f, 0.f};

// The camera position for the CSS perspective projection model.
static constexpr float CSSEyeZ = 1200.f;
// The position of the far plane on the Z axis for the CSS perspective projection model.
static constexpr float CSSFarZ = -500.f;

std::shared_ptr<ImageFilter> ImageFilter::Perspective(const PerspectiveInfo& perspective) {
  return std::make_shared<PerspectiveImageFilter>(perspective);
}

PerspectiveImageFilter::PerspectiveImageFilter(const PerspectiveInfo& info) : info(info) {
  normalProjectMatrix = MakeProjectMatrix(info.projectType, Rect::MakeXYWH(-1.f, -1.f, 2.f, 2.f));
  modelRotateMatrix = Matrix3D::MakeRotate({1.f, 0.f, 0.f}, info.xRotation);
  modelRotateMatrix.postRotate({0.f, 1.f, 0.f}, info.yRotation);
  modelRotateMatrix.postRotate({0.f, 0.f, 1.f}, info.zRotation);
}

Rect PerspectiveImageFilter::onFilterBounds(const Rect& srcRect) const {
  auto normalModelMatrix = modelRotateMatrix;
  normalModelMatrix.postTranslate(0.f, 0.f, info.depth * 2.f / srcRect.height());
  const auto normalTransformMatrix = normalProjectMatrix * normalModelMatrix;
  constexpr auto tempRect = Rect::MakeXYWH(-1.f, -1.f, 2.f, 2.f);
  const auto ndcResult = normalTransformMatrix.mapRect(tempRect);
  const auto normalizedResult =
      Rect::MakeXYWH((ndcResult.left + 1.f) * 0.5f, (ndcResult.top + 1.f) * 0.5f,
                     ndcResult.width() * 0.5f, ndcResult.height() * 0.5f);
  const auto result = Rect::MakeXYWH(normalizedResult.left * srcRect.width() + srcRect.left,
                                     normalizedResult.top * srcRect.height() + srcRect.top,
                                     normalizedResult.width() * srcRect.width(),
                                     normalizedResult.height() * srcRect.height());
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

  // To ensure the correct visual effect of perspective projection, use the rectangle
  // Rect(0, 0, sourceW, sourceH) describing the entire original image to establish the perspective
  // projection model. This ensures that the projection of the rectangle covers the front surface of
  // the clipping frustum when no model transformation is applied.
  const auto projectMatrix = MakeProjectMatrix(info.projectType, srcRect);
  auto modelMatrix = modelRotateMatrix;
  modelMatrix.postTranslate(0.f, 0.f, info.depth);
  const auto transformMatrix = projectMatrix * modelMatrix;

  // ProjectRect is the result of the projection transformation of the rectangle
  // Rect(0, 0, sourceW, sourceH) on the canvas, and RenderBounds describes a region within it.
  // Since the perspective transformation model for calculating NDC is based on the dimensions of
  // the source, and the size of the render target RT is set via RenderBounds, the NDC coordinates
  // are scaled and translated so that RT only displays the required content.
  const Vec2 ndcScale(sourceW / renderBounds.width(), sourceH / renderBounds.height());
  const auto ndcRect = transformMatrix.mapRect(srcRect);
  const auto ndcRectScaled =
      Rect::MakeXYWH(ndcRect.left * ndcScale.x, ndcRect.top * ndcScale.y,
                     ndcRect.width() * ndcScale.x, ndcRect.height() * ndcScale.y);
  const auto normalizedRect =
      Rect::MakeXYWH((ndcRect.left + 1.f) * 0.5f, (ndcRect.top + 1.f) * 0.5f,
                     ndcRect.width() * 0.5f, ndcRect.height() * 0.5f);
  // Align the top-left origin of the drawing area renderBounds with the NDC coordinate origin
  // (-1.f, -1.f) of the clipping rectangle.
  const auto projectRect =
      Rect::MakeXYWH(normalizedRect.left * sourceW, normalizedRect.top * sourceH,
                     normalizedRect.width() * sourceW, normalizedRect.height() * sourceH);
  const Vec2 renderBoundsLTNDC(
      (renderBounds.left - projectRect.left) * ndcRectScaled.width() / projectRect.width(),
      (renderBounds.top - projectRect.top) * ndcRectScaled.height() / projectRect.height());
  const Vec2 ndcOffset(-1.f - ndcRectScaled.left - renderBoundsLTNDC.x,
                       -1.f - ndcRectScaled.top - renderBoundsLTNDC.y);

  const auto drawingManager = args.context->drawingManager();
  const PerspectiveRenderArgs perspectiveArgs{AAType::Coverage, transformMatrix, ndcScale,
                                              ndcOffset};
  drawingManager->addRectPerspectiveRenderTask(srcRect, renderTarget, sourceTextureProxy,
                                               perspectiveArgs);
  return renderTarget->asTextureProxy();
}

PlacementPtr<FragmentProcessor> PerspectiveImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

Matrix3D PerspectiveImageFilter::MakeProjectMatrix(PerspectiveType projectType, const Rect& rect) {
  switch (projectType) {
    case PerspectiveType::Standard: {
      const float eyePositionZ =
          rect.height() * 0.5f / tanf(DegreesToRadians(StandardFovYDegress * 0.5f));
      const Vec3 eyePosition = {0.f, 0.f, eyePositionZ};
      const auto viewMatrix = Matrix3D::LookAt(eyePosition, StandardEyeCenter, StandardEyeUp);
      // Ensure nearZ is not too far away or farZ is not too close to avoid precision issues. For
      // example, if the z value of the near plane is less than 0, the projected model will be
      // outside the clipping range, or if the far plane is too close, the projected model may
      // exceed the clipping range with a slight rotation.
      const float nearZ = std::min(StandardMaxNearZ, eyePositionZ * 0.1f);
      const float farZ = std::max(StandardMinFarZ, eyePositionZ * 10.f);
      const auto perspectiveMatrix =
          Matrix3D::Perspective(StandardFovYDegress, rect.width() / rect.height(), nearZ, farZ);
      return perspectiveMatrix * viewMatrix;
    }
    case PerspectiveType::CSS: {
      // The Y axis of the model coordinate system points downward, while the Y axis of the CSS
      // projection model points upward, so top and bottom need to be swapped.
      const float top = rect.bottom;
      const float bottom = rect.top;
      return Matrix3D::ProjectionCSS(CSSEyeZ, rect.left, rect.right, top, bottom, CSSFarZ);
    }
    default:
      DEBUG_ASSERT(false);
      return Matrix3D::I();
  }
}

}  // namespace tgfx