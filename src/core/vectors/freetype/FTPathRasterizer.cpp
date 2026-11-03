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

#include "FTPathRasterizer.h"
#include "FTLibrary.h"
#include "FTPath.h"
#include "FTRasterTarget.h"
#include "core/NoConicsPathIterator.h"
#include "core/utils/ClearPixels.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/GammaCorrection.h"
#include "core/utils/ShapeUtils.h"
#include "tgfx/core/Path.h"

namespace tgfx {
static void AddPathToFTPath(const Path& path, FTPath* ftPath) {
  NoConicsPathIterator iterator(path);
  for (auto segment : iterator) {
    switch (segment.verb) {
      case PathVerb::Move:
        ftPath->moveTo(segment.points[0]);
        break;
      case PathVerb::Line:
        ftPath->lineTo(segment.points[1]);
        break;
      case PathVerb::Quad:
        ftPath->quadTo(segment.points[1], segment.points[2]);
        break;
      case PathVerb::Cubic:
        ftPath->cubicTo(segment.points[1], segment.points[2], segment.points[3]);
        break;
      case PathVerb::Close:
        ftPath->close();
        break;
      default:
        break;
    }
  }
}

std::shared_ptr<PathRasterizer> PathRasterizer::MakeFrom(int width, int height,
                                                         std::shared_ptr<Shape> shape,
                                                         bool antiAlias,
                                                         bool needsGammaCorrection) {
  if (shape == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::make_shared<FTPathRasterizer>(width, height, std::move(shape), antiAlias,
                                            needsGammaCorrection);
}

bool FTPathRasterizer::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
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
  auto totalMatrix = Matrix::MakeScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(targetInfo.height()));
  path.transform(totalMatrix);
  if (path.isInverseFillType()) {
    Path clipPath = {};
    clipPath.addRect(Rect::MakeWH(targetInfo.width(), targetInfo.height()));
    path.addPath(clipPath, PathOp::Intersect);
  }
  ClearPixels(targetInfo, dstPixels);
  FTPath ftPath = {};
  AddPathToFTPath(path, &ftPath);
  auto fillType = path.getFillType();
  ftPath.setEvenOdd(fillType == PathFillType::EvenOdd || fillType == PathFillType::InverseEvenOdd);
  auto outlines = ftPath.getOutlines();
  auto ftLibrary = FTLibrary::Get();
  if (!antiAlias) {
    // A monochrome bitmap drops even-odd holes, because only the smooth rasterizer honors the fill
    // rule. Render smooth, then binarize.
    FT_Bitmap bitmap;
    bitmap.width = static_cast<unsigned>(targetInfo.width());
    bitmap.rows = static_cast<unsigned>(targetInfo.height());
    bitmap.pitch = static_cast<int>(targetInfo.rowBytes());
    bitmap.buffer = static_cast<unsigned char*>(dstPixels);
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitmap.num_grays = 256;
    for (auto& outline : outlines) {
      FT_Outline_Get_Bitmap(ftLibrary, &(outline->outline), &bitmap);
    }
    auto* pixels = static_cast<unsigned char*>(dstPixels);
    // Area at least half stays. The platform rasterizers use their own hard-edge rules, so curved
    // edges may differ by a pixel.
    constexpr unsigned char COVERAGE_THRESHOLD = 128;
    for (int y = 0; y < targetInfo.height(); y++) {
      auto* row = pixels + static_cast<size_t>(y) * targetInfo.rowBytes();
      for (int x = 0; x < targetInfo.width(); x++) {
        row[x] = row[x] >= COVERAGE_THRESHOLD ? 0xFF : 0x00;
      }
    }
    if (NeedConvertColorSpace(colorSpace(), dstColorSpace)) {
      ConvertColorSpaceInPlace(width(), height(), colorType, alphaType, dstRowBytes, colorSpace(),
                               dstColorSpace, dstPixels);
    }
    return true;
  }
  if (!needsGammaCorrection) {
    FT_Bitmap bitmap;
    bitmap.width = static_cast<unsigned>(targetInfo.width());
    bitmap.rows = static_cast<unsigned>(targetInfo.height());
    bitmap.pitch = static_cast<int>(targetInfo.rowBytes());
    bitmap.buffer = static_cast<unsigned char*>(dstPixels);
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitmap.num_grays = 256;
    for (auto& outline : outlines) {
      FT_Outline_Get_Bitmap(ftLibrary, &(outline->outline), &bitmap);
    }
    if (NeedConvertColorSpace(colorSpace(), dstColorSpace)) {
      ConvertColorSpaceInPlace(width(), height(), colorType, alphaType, dstRowBytes, colorSpace(),
                               dstColorSpace, dstPixels);
    }
    return true;
  }
  auto buffer = static_cast<unsigned char*>(dstPixels);
  auto rows = targetInfo.height();
  // Only anti-aliased draws reach this branch; the non-anti-aliased path above binarizes instead.
  // Gamma correction needs per-pixel coverage, so ask FT for the AA direct raster.
  auto pitch = static_cast<int>(targetInfo.rowBytes());
  FTRasterTarget target = {buffer + (rows - 1) * pitch, pitch,
                           GammaCorrection::GammaTable().data()};
  FT_Raster_Params params;
  params.flags = FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_CLIP | FT_RASTER_FLAG_AA;
  params.gray_spans = GraySpanFunc;
  params.user = &target;
  params.clip_box = {0, 0, static_cast<FT_Pos>(targetInfo.width()),
                     static_cast<FT_Pos>(targetInfo.height())};
  for (const auto& outline : outlines) {
    FT_Outline_Render(ftLibrary, &(outline->outline), &params);
  }
  if (NeedConvertColorSpace(colorSpace(), dstColorSpace)) {
    ConvertColorSpaceInPlace(width(), height(), colorType, alphaType, dstRowBytes, colorSpace(),
                             dstColorSpace, dstPixels);
  }
  return true;
}
}  // namespace tgfx
