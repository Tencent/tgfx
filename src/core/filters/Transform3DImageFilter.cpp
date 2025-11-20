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
#include "core/Matrix2D.h"
#include "core/utils/MathExtra.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/ops/Rect3DDrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {

std::shared_ptr<ImageFilter> ImageFilter::Transform3D(const Matrix3D& matrix, bool hideBackFace) {
  return std::make_shared<Transform3DImageFilter>(matrix, hideBackFace);
}

Transform3DImageFilter::Transform3DImageFilter(const Matrix3D& matrix, bool hideBackFace)
    : _matrix(matrix), _hideBackFace(hideBackFace) {
}

Rect Transform3DImageFilter::onFilterBounds(const Rect& rect, MapDirection mapDirection) const {
  if (mapDirection == MapDirection::Forward) {
    auto result = _matrix.mapRect(rect);
    return result;
  }

  // All vertices inside the rect have an initial z-coordinate of 0, so the third column of the 4x4
  // matrix does not affect the final transformation result and can be ignored. Additionally, since
  // we do not care about the final projected z-axis coordinate, the third row can also be ignored.
  // Therefore, the 4x4 matrix can be simplified to a 3x3 matrix.
  float values[16] = {};
  _matrix.getColumnMajor(values);
  auto matrix2D = Matrix2D::MakeAll(values[0], values[1], values[3], values[4], values[5],
                                    values[7], values[12], values[13], values[15]);
  Matrix2D inversedMatrix;
  if (!matrix2D.invert(&inversedMatrix)) {
    DEBUG_ASSERT(false);
    return rect;
  }
  auto result = inversedMatrix.mapRect(rect);
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

  auto renderTarget = RenderTargetProxy::Make(
      args.context, static_cast<int>(dstDrawWidth), static_cast<int>(dstDrawHeight),
      source->isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft, args.backingFit);
  auto sourceTextureProxy = source->lockTextureProxy(args);

  auto srcW = static_cast<float>(source->width());
  auto srcH = static_cast<float>(source->height());
  // The default transformation anchor is at the top-left origin (0,0) of the image; user-defined
  // anchors are included in the matrix.
  auto srcModelRect = Rect::MakeXYWH(0.f, 0.f, srcW, srcH);
  // SrcProjectRect is the result of projecting srcRect onto the canvas. RenderBounds describes a
  // subregion that needs to be drawn within it.
  auto srcProjectRect = _matrix.mapRect(srcModelRect);
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
      Rect::MakeXYWH(srcProjectRect.left * ndcScale.x, srcProjectRect.top * ndcScale.y,
                     srcProjectRect.width() * ndcScale.x, srcProjectRect.height() * ndcScale.y);
  const Vec2 renderBoundsLTNDCScaled((renderBounds.left - srcProjectRect.left) * ndcScale.x,
                                     (renderBounds.top - srcProjectRect.top) * ndcScale.y);
  const Vec2 ndcOffset(-1.f - ndcRectScaled.left - renderBoundsLTNDCScaled.x,
                       -1.f - ndcRectScaled.top - renderBoundsLTNDCScaled.y);

  auto drawingManager = args.context->drawingManager();
  auto allocator = args.context->drawingAllocator();
  auto vertexProvider = RectsVertexProvider::MakeFrom(allocator, srcModelRect, AAType::Coverage);
  const Size viewportSize(static_cast<float>(renderTarget->width()),
                          static_cast<float>(renderTarget->height()));
  const Rect3DDrawArgs drawArgs{_matrix, ndcScale, ndcOffset, viewportSize};
  auto drawOp =
      Rect3DDrawOp::Make(args.context, std::move(vertexProvider), args.renderFlags, drawArgs);
  const SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  // Ensure the vertex texture sampling coordinates are in the range [0, 1]
  DEBUG_ASSERT(srcW > 0 && srcH > 0);
  // The Size obtained from Source is the original size, while the texture size generated by Source
  // is the size after applying DrawScale. Texture sampling requires corresponding scaling.
  auto uvMatrix = Matrix::MakeScale(static_cast<float>(sourceTextureProxy->width()) / srcW,
                                    static_cast<float>(sourceTextureProxy->height()) / srcH);
  auto fragmentProcessor =
      TextureEffect::Make(allocator, std::move(sourceTextureProxy), samplingArgs, &uvMatrix);
  drawOp->addColorFP(std::move(fragmentProcessor));
  if (_hideBackFace) {
    drawOp->setCullMode(CullMode::Back);
  }
  std::vector<PlacementPtr<DrawOp>> drawOps;
  drawOps.emplace_back(std::move(drawOp));
  auto drawOpArray = allocator->makeArray(std::move(drawOps));
  drawingManager->addOpsRenderTask(renderTarget, std::move(drawOpArray), Color::Transparent());

  return renderTarget->asTextureProxy();
}

PlacementPtr<FragmentProcessor> Transform3DImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

}  // namespace tgfx
