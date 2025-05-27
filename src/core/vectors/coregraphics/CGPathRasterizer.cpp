/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "CGPathRasterizer.h"
#include <CoreGraphics/CGBitmapContext.h>
#include "core/PixelBuffer.h"
#include "core/utils/GammaCorrection.h"
#include "platform/apple/BitmapContextUtil.h"
#include "tgfx/core/PathTypes.h"

namespace tgfx {
static void Iterator(PathVerb verb, const Point points[4], void* info) {
  auto cgPath = reinterpret_cast<CGMutablePathRef>(info);
  switch (verb) {
    case PathVerb::Move:
      CGPathMoveToPoint(cgPath, nullptr, points[0].x, points[0].y);
      break;
    case PathVerb::Line:
      CGPathAddLineToPoint(cgPath, nullptr, points[1].x, points[1].y);
      break;
    case PathVerb::Quad:
      CGPathAddQuadCurveToPoint(cgPath, nullptr, points[1].x, points[1].y, points[2].x,
                                points[2].y);
      break;
    case PathVerb::Cubic:
      CGPathAddCurveToPoint(cgPath, nullptr, points[1].x, points[1].y, points[2].x, points[2].y,
                            points[3].x, points[3].y);
      break;
    case PathVerb::Close:
      CGPathCloseSubpath(cgPath);
      break;
  }
}

static void DrawPath(const Path& path, CGContextRef cgContext, const ImageInfo& info,
                     bool antiAlias) {
  auto cgPath = CGPathCreateMutable();
  path.decompose(Iterator, cgPath);

  CGContextSetShouldAntialias(cgContext, antiAlias);
  static const CGFloat white[] = {1.f, 1.f, 1.f, 1.f};
  if (path.isInverseFillType()) {
    auto rect = CGRectMake(0.f, 0.f, info.width(), info.height());
    CGContextAddRect(cgContext, rect);
    CGContextSetFillColor(cgContext, white);
    CGContextFillPath(cgContext);
    CGContextAddPath(cgContext, cgPath);
    if (path.getFillType() == PathFillType::InverseWinding) {
      CGContextClip(cgContext);
    } else {
      CGContextEOClip(cgContext);
    }
    CGContextClearRect(cgContext, rect);
  } else {
    CGContextAddPath(cgContext, cgPath);
    CGContextSetFillColor(cgContext, white);
    if (path.getFillType() == PathFillType::Winding) {
      CGContextFillPath(cgContext);
    } else {
      CGContextEOFillPath(cgContext);
    }
  }
  CGPathRelease(cgPath);
}

static CGImageRef CreateCGImage(const Path& path, void* pixels, const ImageInfo& info,
                                bool antiAlias, float left, float top,
                                const std::array<uint8_t, 256>& gammaTable) {
  auto cgContext = CreateBitmapContext(info, pixels);
  if (cgContext == nullptr) {
    return nullptr;
  }
  CGContextTranslateCTM(cgContext, -left, -top);
  DrawPath(path, cgContext, info, antiAlias);
  CGContextFlush(cgContext);
  auto* p = static_cast<uint8_t*>(pixels);
  auto stride = info.rowBytes();
  for (int y = 0; y < info.height(); ++y) {
    for (int x = 0; x < info.width(); ++x) {
      p[x] = gammaTable[p[x]];
    }
    p += stride;
  }
  CGContextSynchronize(cgContext);
  auto image = CGBitmapContextCreateImage(cgContext);
  CGContextRelease(cgContext);
  return image;
}

std::shared_ptr<PathRasterizer> PathRasterizer::Make(std::shared_ptr<Shape> shape, bool antiAlias,
                                                     bool needsGammaCorrection) {
  if (shape == nullptr) {
    return nullptr;
  }
  auto bounds = shape->getBounds();
  if (bounds.isEmpty()) {
    return nullptr;
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  return std::make_shared<CGPathRasterizer>(width, height, std::move(shape), antiAlias,
                                            needsGammaCorrection);
}

bool CGPathRasterizer::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstPixels == nullptr || dstInfo.isEmpty()) {
    return false;
  }
  auto path = shape->getPath();
  if (path.isEmpty()) {
    return false;
  }
  auto cgContext = CreateBitmapContext(dstInfo, dstPixels);
  if (cgContext == nullptr) {
    return false;
  }
  auto totalMatrix = Matrix::MakeScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(dstInfo.height()));
  path.transform(totalMatrix);
  auto bounds = path.getBounds();
  bounds.roundOut();
  if (!needsGammaCorrection) {
    DrawPath(path, cgContext, dstInfo, antiAlias);
  }
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  auto tempBuffer = PixelBuffer::Make(width, height, true, false);
  if (tempBuffer == nullptr) {
    CGContextRelease(cgContext);
    return false;
  }
  auto tempPixels = tempBuffer->lockPixels();
  if (tempPixels == nullptr) {
    CGContextRelease(cgContext);
    return false;
  }
  memset(tempPixels, 0, tempBuffer->info().byteSize());
  auto image = CreateCGImage(path, tempPixels, tempBuffer->info(), antiAlias, bounds.left,
                             bounds.top, GammaCorrection::GammaTable());
  tempBuffer->unlockPixels();
  if (image == nullptr) {
    CGContextRelease(cgContext);
    return false;
  }
  auto rect = CGRectMake(bounds.left, bounds.top, bounds.width(), bounds.height());
  CGContextDrawImage(cgContext, rect, image);
  CGContextRelease(cgContext);
  CGImageRelease(image);
  return true;
}

}  // namespace tgfx
