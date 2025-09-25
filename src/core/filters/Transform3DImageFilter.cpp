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
#include "core/utils/PlacementPtr.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/ops/Rect3DDrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {

std::shared_ptr<ImageFilter> ImageFilter::Transform3D(const Matrix3D& matrix,
                                                      const Size& viewSize) {
  return std::make_shared<Transform3DImageFilter>(matrix, viewSize);
}

Transform3DImageFilter::Transform3DImageFilter(const Matrix3D& matrix, const Size& viewportSize)
    : matrix(matrix), viewportSize(viewportSize) {
}

Rect Transform3DImageFilter::onFilterBounds(const Rect& srcRect) const {
  // Align the camera center with the center of the source rect.
  //
  // The standard rectangle XYWH(-viewSize.width * 0.5, -viewSize.height * 0.5, viewSize.width,
  // viewSize.height). After applying the matrix, the projected rectangle in the NDC coordinate
  // system is XYWH[-1, 1, 2, 2]. For convenience, the top-left vertex of the standard rectangle is
  // set to (0,0). Combined with srcRect, a model rectangle is constructed. The minimum axis-aligned
  // bounding rectangle of srcRect after projection is calculated based on the size after applying
  // the matrix and its relative position to the standard rectangle.
  auto srcModelRect = Rect::MakeXYWH(-srcRect.width() * 0.5f, -srcRect.height() * 0.5f,
                                     srcRect.width(), srcRect.height());
  auto ndcRect = matrix.mapRect(srcModelRect);
  auto result = Rect::MakeXYWH(
      ndcRect.left * viewportSize.width * 0.5f - srcModelRect.left + srcRect.left,
      ndcRect.top * viewportSize.height * 0.5f - srcModelRect.top + srcRect.top,
      ndcRect.width() * viewportSize.width * 0.5f, ndcRect.height() * viewportSize.height * 0.5f);
  return result;
}

std::shared_ptr<TextureProxy> Transform3DImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& renderBounds, const TPArgs& args) const {
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(renderBounds.width()), static_cast<int>(renderBounds.height()),
      source->isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft, args.backingFit);
  auto sourceTextureProxy = source->lockTextureProxy(args);

  auto srcW = static_cast<float>(source->width());
  auto srcH = static_cast<float>(source->height());
  // Align the camera center with the initial position center of the source model.
  auto srcModelRect = Rect::MakeXYWH(-srcW * 0.5f, -srcH * 0.5f, srcW, srcH);
  auto srcNDCRect = matrix.mapRect(srcModelRect);
  // SrcProjectRect is the result of projecting srcRect onto the canvas. RenderBounds describes a
  // subregion that needs to be drawn within it.
  auto srcProjectRect =
      Rect::MakeXYWH(srcNDCRect.left * viewportSize.width * 0.5f - srcModelRect.left,
                     srcNDCRect.top * viewportSize.height * 0.5f - srcModelRect.top,
                     srcNDCRect.width() * viewportSize.width * 0.5f,
                     srcNDCRect.height() * viewportSize.height * 0.5f);
  // ndcScale and ndcOffset are used to scale and translate the NDC coordinates to ensure that only
  // the content within RenderBounds is drawn to the render target. This clips regions beyond the
  // clip space.
  // NdcScale first maps the NDC coordinates from the view size defined by this->matrix's projection
  // to the size of srcProjectRect after source projection, then scales up so that the required
  // drawing area exactly fills the -1 to 1 clip region. The scale formula is:
  // ((viewsize / srcProjectRect) * (srcProjectRect / renderBounds))
  const Vec2 ndcScale(viewportSize.width / renderBounds.width(),
                      viewportSize.height / renderBounds.height());
  // ndcOffset translates the NDC coordinates so that the local area to be drawn aligns exactly with
  // the (-1, -1) point.
  auto ndcRectScaled =
      Rect::MakeXYWH(srcNDCRect.left * ndcScale.x, srcNDCRect.top * ndcScale.y,
                     srcNDCRect.width() * ndcScale.x, srcNDCRect.height() * ndcScale.y);
  const Vec2 renderBoundsLTNDC(
      (renderBounds.left - srcProjectRect.left) * ndcRectScaled.width() / srcProjectRect.width(),
      (renderBounds.top - srcProjectRect.top) * ndcRectScaled.height() / srcProjectRect.height());
  const Vec2 ndcOffset(-1.f - ndcRectScaled.left - renderBoundsLTNDC.x,
                       -1.f - ndcRectScaled.top - renderBoundsLTNDC.y);

  auto drawingManager = args.context->drawingManager();
  auto drawingBuffer = args.context->drawingBuffer();
  auto vertexProvider =
      RectsVertexProvider::MakeFrom(drawingBuffer, srcModelRect, AAType::Coverage);
  const Size viewportSise(static_cast<float>(renderTarget->width()),
                          static_cast<float>(renderTarget->height()));
  const Rect3DDrawArgs drawArgs{matrix, ndcScale, ndcOffset, viewportSise};
  auto drawOp =
      Rect3DDrawOp::Make(args.context, std::move(vertexProvider), args.renderFlags, drawArgs);
  const SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  // Ensure the vertex texture sampling coordinates are in the range [0, 1]
  auto uvMatrix = Matrix::MakeTrans(-srcModelRect.left, -srcModelRect.top);
  auto fragmentProcessor =
      TextureEffect::Make(std::move(sourceTextureProxy), samplingArgs, &uvMatrix);
  drawOp->addColorFP(std::move(fragmentProcessor));
  std::vector<PlacementPtr<DrawOp>> drawOps;
  drawOps.emplace_back(std::move(drawOp));
  auto drawOpArray = drawingBuffer->makeArray(std::move(drawOps));
  drawingManager->addOpsRenderTask(renderTarget, std::move(drawOpArray), std::nullopt);

  return renderTarget->asTextureProxy();
}

PlacementPtr<FragmentProcessor> Transform3DImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

}  // namespace tgfx