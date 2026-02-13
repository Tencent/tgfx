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

#include "CGPathRasterizer.h"
#include <CoreGraphics/CGBitmapContext.h>
#include "core/NoConicsPathIterator.h"
#include "core/PixelBuffer.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/GammaCorrection.h"
#include "core/utils/MathExtra.h"
#include "core/utils/ShapeUtils.h"
#include "platform/apple/BitmapContextUtil.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"

namespace tgfx {
static void AddPathToCGPath(const Path& path, CGMutablePathRef cgPath) {
  NoConicsPathIterator iterator(path);
  for (auto segment : iterator) {
    switch (segment.verb) {
      case PathVerb::Move:
        CGPathMoveToPoint(cgPath, nullptr, segment.points[0].x, segment.points[0].y);
        break;
      case PathVerb::Line:
        CGPathAddLineToPoint(cgPath, nullptr, segment.points[1].x, segment.points[1].y);
        break;
      case PathVerb::Quad:
        CGPathAddQuadCurveToPoint(cgPath, nullptr, segment.points[1].x, segment.points[1].y,
                                  segment.points[2].x, segment.points[2].y);
        break;
      case PathVerb::Cubic:
        CGPathAddCurveToPoint(cgPath, nullptr, segment.points[1].x, segment.points[1].y,
                              segment.points[2].x, segment.points[2].y, segment.points[3].x,
                              segment.points[3].y);
        break;
      case PathVerb::Close:
        CGPathCloseSubpath(cgPath);
        break;
      default:
        break;
    }
  }
}

static void DrawPath(const Path& path, CGContextRef cgContext, const ImageInfo& info,
                     bool antiAlias) {
  auto cgPath = CGPathCreateMutable();
  AddPathToCGPath(path, cgPath);

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
  auto p = static_cast<uint8_t*>(pixels);
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

std::shared_ptr<PathRasterizer> PathRasterizer::MakeFrom(int width, int height,
                                                         std::shared_ptr<Shape> shape,
                                                         bool antiAlias,
                                                         bool needsGammaCorrection) {
  if (shape == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::make_shared<CGPathRasterizer>(width, height, std::move(shape), antiAlias,
                                            needsGammaCorrection);
}

bool CGPathRasterizer::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                                    std::shared_ptr<ColorSpace> dstColorSpace,
                                    void* dstPixels) const {
  if (dstPixels == nullptr) {
    return false;
  }
  auto path = shape->getPath();
  if (path.isEmpty()) {
    return false;
  }
  auto dstInfo =
      ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, dstColorSpace);
  auto targetInfo = dstInfo.makeIntersect(0, 0, width(), height());
  auto cgContext = CreateBitmapContext(targetInfo, dstPixels);
  if (cgContext == nullptr) {
    return false;
  }
  CGContextClearRect(cgContext, CGRectMake(0.f, 0.f, targetInfo.width(), targetInfo.height()));
  auto totalMatrix = Matrix::MakeScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(targetInfo.height()));
  path.transform(totalMatrix);
  if (!needsGammaCorrection) {
    DrawPath(path, cgContext, targetInfo, antiAlias);
  }
  auto bounds = path.getBounds();
  auto clipBounds = Rect::MakeWH(targetInfo.width(), targetInfo.height());
  if (!bounds.intersect(clipBounds)) {
    return false;
  }
  auto width = FloatCeilToInt(bounds.width());
  auto height = FloatCeilToInt(bounds.height());
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
  if (NeedConvertColorSpace(colorSpace(), dstColorSpace)) {
    ConvertColorSpaceInPlace(ImageGenerator::width(), ImageGenerator::height(), colorType,
                             alphaType, dstRowBytes, colorSpace(), dstColorSpace, dstPixels);
  }
  return true;
}

}  // namespace tgfx
