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

#include "Transform3DImageFilter.h"
#include "core/utils/MathExtra.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/ops/Rect3DDrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {

std::shared_ptr<ImageFilter> ImageFilter::Transform3D(const Matrix3D& matrix) {
  return std::make_shared<Transform3DImageFilter>(matrix);
}

Transform3DImageFilter::Transform3DImageFilter(const Matrix3D& matrix) : matrix(matrix) {
}

Rect Transform3DImageFilter::onFilterBounds(const Rect& srcRect) const {
  // Align the camera center with the center of the source rect.
  auto srcModelRect = Rect::MakeXYWH(-srcRect.width() * 0.5f, -srcRect.height() * 0.5f,
                                     srcRect.width(), srcRect.height());
  auto dstModelRect = matrix.mapRect(srcModelRect);
  // The minimum axis-aligned bounding rectangle of srcRect after projection is calculated based on
  // its relative position to the standard rectangle.
  auto result = Rect::MakeXYWH(dstModelRect.left - srcModelRect.left + srcRect.left,
                               dstModelRect.top - srcModelRect.top + srcRect.top,
                               dstModelRect.width(), dstModelRect.height());
  return result;
}

std::shared_ptr<TextureProxy> Transform3DImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& renderBounds, const TPArgs& args) const {
  float dstDrawWidth = renderBounds.width();
  float dstDrawHeight = renderBounds.height();
  DEBUG_ASSERT(args.drawScale > 0.f);
  if (!FloatNearlyEqual(args.drawScale, 1.f)) {
    dstDrawWidth = dstDrawWidth * args.drawScale;
    dstDrawHeight = dstDrawHeight * args.drawScale;
  }
  dstDrawWidth = std::ceil(dstDrawWidth);
  dstDrawHeight = std::ceil(dstDrawHeight);
  const float drawScaleX = dstDrawWidth / renderBounds.width();
  const float drawScaleY = dstDrawHeight / renderBounds.height();

  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(dstDrawWidth), static_cast<int>(dstDrawHeight),
      source->isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft, args.backingFit);
  auto sourceTextureProxy = source->lockTextureProxy(args);

  auto srcW = static_cast<float>(source->width());
  auto srcH = static_cast<float>(source->height());
  // Align the camera center with the initial position center of the source model.
  auto srcModelRect = Rect::MakeXYWH(-srcW * 0.5f, -srcH * 0.5f, srcW, srcH);
  auto dstModelRect = matrix.mapRect(srcModelRect);
  // SrcProjectRect is the result of projecting srcRect onto the canvas. RenderBounds describes a
  // subregion that needs to be drawn within it.
  auto srcProjectRect =
      Rect::MakeXYWH(dstModelRect.left - srcModelRect.left, dstModelRect.top - srcModelRect.top,
                     dstModelRect.width(), dstModelRect.height());
  // ndcScale and ndcOffset are used to scale and translate the NDC coordinates to ensure that only
  // the content within RenderBounds is drawn to the render target. This clips regions beyond the
  // clip space.
  // NdcScale first maps the projected coordinates to the NDC region [-1, 1], then scales them so
  // that the required drawing area exactly fills the [-1, 1] clip region. The scaling formula is:
  // ((2 / srcProjectRect) * (srcProjectRect / renderBounds))
  // Scaling the original image with drawScale does not affect the calculation result of ndcScale.
  const Vec2 ndcScale(2.0f / renderBounds.width(), 2.0f / renderBounds.height());
  // ndcOffset translates the NDC coordinates so that the local area to be drawn aligns exactly with
  // the (-1, -1) point.
  auto ndcRectScaled =
      Rect::MakeXYWH(dstModelRect.left * ndcScale.x, dstModelRect.top * ndcScale.y,
                     dstModelRect.width() * ndcScale.x, dstModelRect.height() * ndcScale.y);
  const Vec2 renderBoundsLTNDCScaled((renderBounds.left - srcProjectRect.left) * ndcScale.x,
                                     (renderBounds.top - srcProjectRect.top) * ndcScale.y);
  const Vec2 ndcOffset(-1.f - ndcRectScaled.left - renderBoundsLTNDCScaled.x,
                       -1.f - ndcRectScaled.top - renderBoundsLTNDCScaled.y);

  auto drawingManager = args.context->drawingManager();
  auto drawingBuffer = args.context->drawingBuffer();
  auto vertexProvider =
      RectsVertexProvider::MakeFrom(drawingBuffer, srcModelRect, AAType::Coverage);
  const Size viewportSize(static_cast<float>(renderTarget->width()),
                          static_cast<float>(renderTarget->height()));
  const Rect3DDrawArgs drawArgs{matrix, ndcScale, ndcOffset, viewportSize};
  auto drawOp =
      Rect3DDrawOp::Make(args.context, std::move(vertexProvider), args.renderFlags, drawArgs);
  const SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  // Ensure the vertex texture sampling coordinates are in the range [0, 1]
  auto uvMatrix = Matrix::MakeTrans(-srcModelRect.left, -srcModelRect.top);
  // The Size obtained from Source is the original size, while the texture size generated by Source
  // is the size after applying DrawScale. Texture sampling requires corresponding scaling.
  uvMatrix.postScale(drawScaleX, drawScaleY);
  auto fragmentProcessor =
      TextureEffect::Make(std::move(sourceTextureProxy), samplingArgs, &uvMatrix);
  drawOp->addColorFP(std::move(fragmentProcessor));
  std::vector<PlacementPtr<DrawOp>> drawOps;
  drawOps.emplace_back(std::move(drawOp));
  auto drawOpArray = drawingBuffer->makeArray(std::move(drawOps));
  drawingManager->addOpsRenderTask(renderTarget, std::move(drawOpArray), Color::Transparent());

  return renderTarget->asTextureProxy();
}

PlacementPtr<FragmentProcessor> Transform3DImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

}  // namespace tgfx