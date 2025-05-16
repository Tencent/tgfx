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

#include "FTGlyphDrawer.h"
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

std::shared_ptr<GlyphDrawer> GlyphDrawer::Make(float resolutionScale, bool antiAlias,
                                               bool needsGammaCorrection) {
  return std::make_shared<FTGlyphDrawer>(resolutionScale, antiAlias, needsGammaCorrection);
}

bool FTGlyphDrawer::onFillPath(const Path& path, const ImageInfo& dstInfo, void* dstPixels) {
  auto finalPath = path;
  auto totalMatrix = Matrix::MakeScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(dstInfo.height()));
  finalPath.transform(totalMatrix);
  if (finalPath.isInverseFillType()) {
    Path maskPath = {};
    maskPath.addRect(Rect::MakeWH(dstInfo.width(), dstInfo.height()));
    finalPath.addPath(maskPath, PathOp::Intersect);
  }
  FTPath ftPath = {};
  finalPath.decompose(Iterator, &ftPath);
  auto fillType = finalPath.getFillType();
  ftPath.setEvenOdd(fillType == PathFillType::EvenOdd || fillType == PathFillType::InverseEvenOdd);
  auto outlines = ftPath.getOutlines();
  auto ftLibrary = FTLibrary::Get();
  auto buffer = static_cast<unsigned char*>(dstPixels);
  auto rows = dstInfo.height();
  auto pitch = static_cast<int>(dstInfo.rowBytes());
  RasterTarget target = {buffer + (rows - 1) * pitch, pitch, GlyphDrawer::GammaTable().data()};
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