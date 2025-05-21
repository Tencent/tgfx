/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Stroke.h"
#include "core/PathRef.h"

namespace tgfx {
using namespace pk;

static SkPaint::Cap ToSkLineCap(LineCap cap) {
  switch (cap) {
    case LineCap::Round:
      return SkPaint::kRound_Cap;
    case LineCap::Square:
      return SkPaint::kSquare_Cap;
    default:
      return SkPaint::kButt_Cap;
  }
}

static SkPaint::Join ToSkLineJoin(LineJoin join) {
  switch (join) {
    case LineJoin::Round:
      return SkPaint::kRound_Join;
    case LineJoin::Bevel:
      return SkPaint::kBevel_Join;
    default:
      return SkPaint::kMiter_Join;
  }
}

bool Stroke::applyToPath(Path* path, float resolutionScale) const {
  if (path == nullptr) {
    return false;
  }
  if (width <= 0) {
    path->reset();
    return true;
  }
  SkPaint paint = {};
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(width);
  paint.setStrokeCap(ToSkLineCap(cap));
  paint.setStrokeJoin(ToSkLineJoin(join));
  paint.setStrokeMiter(miterLimit);
  auto& skPath = PathRef::WriteAccess(*path);
  return paint.getFillPath(skPath, &skPath, nullptr, resolutionScale);
}

void Stroke::applyToBounds(Rect* bounds, bool ignoreMiterLimit) const {
  if (bounds == nullptr) {
    return;
  }
  auto expand = width * 0.5f;
  if (!ignoreMiterLimit && join == LineJoin::Miter) {
    expand *= miterLimit;
  }
  expand = ceilf(expand);
  bounds->outset(expand, expand);
}

}  // namespace tgfx