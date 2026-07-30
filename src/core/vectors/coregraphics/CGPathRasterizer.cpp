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
                                    void* dstPixels) const {
  if (dstPixels == nullptr) {
    return false;
  }
  auto path = shape->getPath();
  if (path.isEmpty()) {
    return false;
  }
  auto dstInfo = ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes);
  auto targetInfo = dstInfo.makeIntersect(0, 0, width(), height());
  auto cgContext = CreateBitmapContext(targetInfo, dstPixels);
  if (cgContext == nullptr) {
    return false;
  }
  CGContextClearRect(cgContext, CGRectMake(0.f, 0.f, targetInfo.width(), targetInfo.height()));
  auto totalMatrix = Matrix::MakeScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(targetInfo.height()));
  path.transform(totalMatrix);
  DrawPath(path, cgContext, targetInfo, antiAlias);
  if (needsGammaCorrection) {
    auto* pixels = static_cast<uint8_t*>(dstPixels);
    auto bpp = targetInfo.bytesPerPixel();
    auto gammaTable = GammaCorrection::GammaTable();
    if (targetInfo.isAlphaOnly()) {
      for (int y = 0; y < targetInfo.height(); ++y) {
        for (int x = 0; x < targetInfo.width(); ++x) {
          pixels[x] = gammaTable[pixels[x]];
        }
        pixels += dstRowBytes;
      }
    } else {
      auto alphaOffset = static_cast<int>(bpp) - 1;
      for (int y = 0; y < targetInfo.height(); ++y) {
        for (int x = 0; x < targetInfo.width(); ++x) {
          pixels[x * bpp + alphaOffset] = gammaTable[pixels[x * bpp + alphaOffset]];
        }
        pixels += dstRowBytes;
      }
    }
  }
  CGContextRelease(cgContext);
  return true;
}

}  // namespace tgfx
