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

#include "FTPathRasterizer.h"
#include "FTLibrary.h"
#include "FTPath.h"

namespace tgfx {
static void Iterator(PathVerb verb, const Point points[4], void* info) {
  auto path = reinterpret_cast<FTPath*>(info);
  switch (verb) {
    case PathVerb::Move:
      path->moveTo(points[0]);
      break;
    case PathVerb::Line:
      path->lineTo(points[1]);
      break;
    case PathVerb::Quad:
      path->quadTo(points[1], points[2]);
      break;
    case PathVerb::Cubic:
      path->cubicTo(points[1], points[2], points[3]);
      break;
    case PathVerb::Close:
      path->close();
      break;
  }
}

struct RasterTarget {
  unsigned char* origin;
  int pitch;
  const uint8_t* gammaTable;
};

static void SpanFunc(int y, int count, const FT_Span* spans, void* user) {
  auto* target = reinterpret_cast<RasterTarget*>(user);
  for (int i = 0; i < count; i++) {
    auto* q = target->origin - target->pitch * y + spans[i].x;
    auto c = target->gammaTable[spans[i].coverage];
    auto aCount = spans[i].len;
    /**
     * For small-spans it is faster to do it by ourselves than calling memset.
     * This is mainly due to the cost of the function call.
     */
    switch (aCount) {
      case 7:
        *q++ = c;
      case 6:
        *q++ = c;
      case 5:
        *q++ = c;
      case 4:
        *q++ = c;
      case 3:
        *q++ = c;
      case 2:
        *q++ = c;
      case 1:
        *q = c;
      case 0:
        break;
      default:
        memset(q, c, aCount);
    }
  }
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
  return std::make_shared<FTPathRasterizer>(width, height, std::move(shape), antiAlias,
                                            needsGammaCorrection);
}

bool FTPathRasterizer::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstPixels == nullptr || dstInfo.isEmpty()) {
    return false;
  }
  auto path = shape->getPath();
  if (path.isEmpty()) {
    return false;
  }
  auto totalMatrix = Matrix::MakeScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(dstInfo.height()));
  path.transform(totalMatrix);
  if (path.isInverseFillType()) {
    Path maskPath = {};
    maskPath.addRect(Rect::MakeWH(dstInfo.width(), dstInfo.height()));
    path.addPath(maskPath, PathOp::Intersect);
  }
  FTPath ftPath = {};
  path.decompose(Iterator, &ftPath);
  auto fillType = path.getFillType();
  ftPath.setEvenOdd(fillType == PathFillType::EvenOdd || fillType == PathFillType::InverseEvenOdd);
  auto outlines = ftPath.getOutlines();
  auto ftLibrary = FTLibrary::Get();
  if (!needsGammaCorrection) {
    FT_Bitmap bitmap;
    bitmap.width = static_cast<unsigned>(dstInfo.width());
    bitmap.rows = static_cast<unsigned>(dstInfo.height());
    bitmap.pitch = static_cast<int>(dstInfo.rowBytes());
    bitmap.buffer = static_cast<unsigned char*>(dstPixels);
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitmap.num_grays = 256;
    for (auto& outline : outlines) {
      FT_Outline_Get_Bitmap(ftLibrary, &(outline->outline), &bitmap);
    }
    return true;
  }
  auto buffer = static_cast<unsigned char*>(dstPixels);
  auto rows = dstInfo.height();
  // Anti-aliasing is always enabled because FreeType generates only 1-bit masks when it's off,
  // and we haven't implemented conversion from 1-bit to 8-bit masks yet.
  auto pitch = static_cast<int>(dstInfo.rowBytes());
  RasterTarget target = {buffer + (rows - 1) * pitch, pitch, PathRasterizer::GammaTable().data()};
  FT_Raster_Params params;
  params.flags = FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_CLIP | FT_RASTER_FLAG_AA;
  params.gray_spans = SpanFunc;
  params.user = &target;
  params.clip_box = {0, 0, static_cast<FT_Pos>(dstInfo.width()),
                     static_cast<FT_Pos>(dstInfo.height())};
  for (const auto& outline : outlines) {
    FT_Outline_Render(ftLibrary, &(outline->outline), &params);
  }
  return true;
}
}  // namespace tgfx
