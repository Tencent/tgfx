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

#include "CGMask.h"
#include "CGTypeface.h"
#include "core/GlyphRunList.h"
#include "core/ScalerContext.h"
#include "platform/apple/BitmapContextUtil.h"
#include "tgfx/core/GlyphFace.h"
#include "tgfx/core/Mask.h"
#include "tgfx/core/Pixmap.h"

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

std::shared_ptr<Mask> Mask::Make(int width, int height, bool tryHardware) {
  auto pixelRef = PixelRef::Make(width, height, true, tryHardware);
  if (pixelRef == nullptr) {
    return nullptr;
  }
  pixelRef->clear();
  return std::make_shared<CGMask>(std::move(pixelRef));
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

void CGMask::onFillPath(const Path& path, const Matrix& matrix, bool antiAlias,
                        bool needsGammaCorrection) {
  if (path.isEmpty()) {
    return;
  }
  auto pixels = pixelRef->lockWritablePixels();
  if (pixels == nullptr) {
    return;
  }
  const auto& info = pixelRef->info();
  auto cgContext = CreateBitmapContext(info, pixels);
  if (cgContext == nullptr) {
    pixelRef->unlockPixels();
    return;
  }

  auto finalPath = path;
  auto totalMatrix = matrix;
  totalMatrix.postScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(info.height()));
  finalPath.transform(totalMatrix);
  auto bounds = finalPath.getBounds();
  bounds.roundOut();
  markContentDirty(bounds, true);

  if (!needsGammaCorrection) {
    DrawPath(finalPath, cgContext, info, antiAlias);
    CGContextRelease(cgContext);
    pixelRef->unlockPixels();
    return;
  }
  int width = static_cast<int>(bounds.width());
  int height = static_cast<int>(bounds.height());
  auto tempBuffer = PixelBuffer::Make(width, height, true, false);
  if (tempBuffer == nullptr) {
    CGContextRelease(cgContext);
    pixelRef->unlockPixels();
    return;
  }
  auto* tempPixels = tempBuffer->lockPixels();
  if (tempPixels == nullptr) {
    CGContextRelease(cgContext);
    pixelRef->unlockPixels();
    return;
  }
  memset(tempPixels, 0, tempBuffer->info().byteSize());
  auto image = CreateCGImage(finalPath, tempPixels, tempBuffer->info(), antiAlias, bounds.left,
                             bounds.top, PixelRefMask::GammaTable());
  tempBuffer->unlockPixels();
  if (image == nullptr) {
    CGContextRelease(cgContext);
    pixelRef->unlockPixels();
    return;
  }
  auto rect = CGRectMake(bounds.left, bounds.top, bounds.width(), bounds.height());
  CGContextDrawImage(cgContext, rect, image);
  CGContextRelease(cgContext);
  pixelRef->unlockPixels();
  CGImageRelease(image);
}

static CGAffineTransform MatrixToCGAffineTransform(const Matrix& matrix) {
  return CGAffineTransformMake(
      static_cast<CGFloat>(matrix.getScaleX()), -static_cast<CGFloat>(matrix.getSkewY()),
      -static_cast<CGFloat>(matrix.getSkewX()), static_cast<CGFloat>(matrix.getScaleY()),
      static_cast<CGFloat>(matrix.getTranslateX()), static_cast<CGFloat>(matrix.getTranslateY()));
}

bool CGMask::onFillText(const GlyphRunList* glyphRunList, const Stroke* stroke,
                        const Matrix& matrix, bool antiAlias) {
  if (glyphRunList == nullptr || stroke) {
    return false;
  }
  for (auto& glyphRun : glyphRunList->glyphRuns()) {
    auto glyphFace = glyphRun.glyphFace;
    if (glyphFace == nullptr || glyphFace->asFont() == std::nullopt) {
      continue;
    }

    const auto& font = glyphFace->asFont();
    if (font->isFauxBold() || font->getTypeface() == nullptr) {
      return false;
    }
  }
  auto pixels = pixelRef->lockWritablePixels();
  auto cgContext = CreateBitmapContext(pixelRef->info(), pixels);
  if (cgContext == nullptr) {
    pixelRef->unlockPixels();
    return false;
  }
  CGContextSetBlendMode(cgContext, kCGBlendModeCopy);
  CGContextSetTextDrawingMode(cgContext, kCGTextFill);
  CGContextSetGrayFillColor(cgContext, 1.0f, 1.0f);
  CGContextSetShouldAntialias(cgContext, antiAlias);
  CGContextSetShouldSmoothFonts(cgContext, true);
  CGContextSetAllowsFontSubpixelQuantization(cgContext, false);
  CGContextSetShouldSubpixelQuantizeFonts(cgContext, false);
  CGContextSetAllowsFontSubpixelPositioning(cgContext, true);
  CGContextSetShouldSubpixelPositionFonts(cgContext, true);

  for (auto& glyphRun : glyphRunList->glyphRuns()) {
    auto glyphFace = glyphRun.glyphFace;
    if (glyphFace == nullptr || glyphFace->asFont() == std::nullopt) {
      continue;
    }

    CGContextSaveGState(cgContext);
    const auto& font = glyphFace->asFont();
    auto typeface = std::static_pointer_cast<CGTypeface>(font->getTypeface());
    CTFontRef ctFont = typeface->getCTFont();
    ctFont = CTFontCreateCopyWithAttributes(ctFont, static_cast<CGFloat>(font->getSize()), nullptr,
                                            nullptr);
    if (font->isFauxItalic()) {
      CGContextSetTextMatrix(cgContext, CGAffineTransformMake(1, 0, -ITALIC_SKEW, 1, 0, 0));
    }
    CGContextTranslateCTM(cgContext, 0.f, static_cast<CGFloat>(height()));
    CGContextScaleCTM(cgContext, 1.f, -1.f);
    CGContextConcatCTM(cgContext, MatrixToCGAffineTransform(matrix));
    auto& glyphIDs = glyphRun.glyphs;
    auto& positions = glyphRun.positions;
    auto glyphCount = glyphIDs.size();
    auto point = CGPointZero;
    for (size_t i = 0; i < glyphCount; ++i) {
      auto position = positions[i];
      CGContextSaveGState(cgContext);
      CGContextTranslateCTM(cgContext, position.x, position.y);
      CGContextScaleCTM(cgContext, 1.f, -1.f);
      CTFontDrawGlyphs(ctFont, static_cast<const CGGlyph*>(&glyphIDs[i]), &point, 1, cgContext);
      CGContextRestoreGState(cgContext);
    }
    CFRelease(ctFont);
    CGContextRestoreGState(cgContext);
  }
  CGContextRelease(cgContext);
  pixelRef->unlockPixels();
  return true;
}
}  // namespace tgfx
