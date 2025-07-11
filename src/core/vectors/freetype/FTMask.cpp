/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "FTMask.h"
#include "FTLibrary.h"
#include "FTPath.h"
#include "FTRasterTarget.h"
#include "core/utils/USE.h"
#include "tgfx/core/Pixmap.h"

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

std::shared_ptr<Mask> Mask::Make(int width, int height, bool tryHardware) {
  auto pixelRef = PixelRef::Make(width, height, true, tryHardware);
  if (pixelRef == nullptr) {
    return nullptr;
  }
  pixelRef->clear();
  return std::make_shared<FTMask>(std::move(pixelRef));
}

void FTMask::onFillPath(const Path& path, const Matrix& matrix, bool antiAlias,
                        bool needsGammaCorrection) {
  if (path.isEmpty()) {
    return;
  }
  auto pixels = pixelRef->lockWritablePixels();
  if (pixels == nullptr) {
    return;
  }
  // We ignore the antiAlias parameter because FreeType always produces 1-bit masks when antiAlias
  // is disabled, and we haven't implemented the conversion from 1-bit to 8-bit masks.
  USE(antiAlias);
  const auto& info = pixelRef->info();
  auto finalPath = path;
  auto totalMatrix = matrix;
  totalMatrix.postScale(1, -1);
  totalMatrix.postTranslate(0, static_cast<float>(pixelRef->height()));
  finalPath.transform(totalMatrix);
  if (finalPath.isInverseFillType()) {
    Path maskPath = {};
    maskPath.addRect(Rect::MakeWH(info.width(), info.height()));
    finalPath.addPath(maskPath, PathOp::Intersect);
  }
  auto bounds = finalPath.getBounds();
  bounds.roundOut();
  markContentDirty(bounds, true);
  FTPath ftPath = {};
  finalPath.decompose(Iterator, &ftPath);
  auto fillType = finalPath.getFillType();
  ftPath.setEvenOdd(fillType == PathFillType::EvenOdd || fillType == PathFillType::InverseEvenOdd);
  auto outlines = ftPath.getOutlines();
  auto ftLibrary = FTLibrary::Get();
  if (!needsGammaCorrection) {
    FT_Bitmap bitmap;
    bitmap.width = static_cast<unsigned>(info.width());
    bitmap.rows = static_cast<unsigned>(info.height());
    bitmap.pitch = static_cast<int>(info.rowBytes());
    bitmap.buffer = static_cast<unsigned char*>(pixels);
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitmap.num_grays = 256;
    for (auto& outline : outlines) {
      FT_Outline_Get_Bitmap(ftLibrary, &(outline->outline), &bitmap);
    }
    pixelRef->unlockPixels();
    return;
  }
  auto buffer = static_cast<unsigned char*>(pixels);
  int rows = info.height();
  int pitch = static_cast<int>(info.rowBytes());
  FTRasterTarget target = {};
  target.origin = buffer + (rows - 1) * pitch;
  target.pitch = pitch;
  target.gammaTable = PixelRefMask::GammaTable().data();
  FT_Raster_Params params;
  params.flags = FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_CLIP | FT_RASTER_FLAG_AA;
  params.gray_spans = GraySpanFunc;
  params.user = &target;
  auto& clip = params.clip_box;
  clip.xMin = 0;
  clip.yMin = 0;
  clip.xMax = (FT_Pos)info.width();
  clip.yMax = (FT_Pos)info.height();
  for (auto& outline : outlines) {
    FT_Outline_Render(ftLibrary, &(outline->outline), &params);
  }
  pixelRef->unlockPixels();
}
}  // namespace tgfx
