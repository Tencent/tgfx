/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/layers/vectors/Ellipse.h"
#include <algorithm>
#include "VectorContext.h"
#include "core/PathRef.h"
#include "core/utils/Log.h"
#include "pathkit.h"

namespace tgfx {

std::shared_ptr<Ellipse> Ellipse::Make() {
  return std::shared_ptr<Ellipse>(new Ellipse());
}

void Ellipse::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Ellipse::setSize(const Size& value) {
  if (_size == value) {
    return;
  }
  _size = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Ellipse::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Ellipse::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_cachedShape == nullptr) {
    constexpr float MinExtent = 5e-3f;
    auto width = std::max(_size.width, MinExtent);
    auto height = std::max(_size.height, MinExtent);
    auto halfWidth = width * 0.5f;
    auto halfHeight = height * 0.5f;
    auto rect = Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight, width, height);
    Path path;
    path.addOval(rect, _reversed);

    // Debug: dump the generated ellipse path
    LOGI("[EllipseDump] size=(%g, %g) effective=(%g, %g) bounds=[%g,%g,%g,%g]",
         _size.width, _size.height, width, height,
         path.getBounds().left, path.getBounds().top, path.getBounds().right,
         path.getBounds().bottom);
    const auto& skPath = PathRef::ReadAccess(path);
    LOGI("[EllipseDump] verbs=%d points=%d isOval=%d fillType=%d",
         skPath.countVerbs(), skPath.countPoints(), skPath.isOval(nullptr),
         static_cast<int>(skPath.getFillType()));
    pk::SkPath::Iter iter(skPath, false);
    pk::SkPoint pts[4];
    pk::SkPath::Verb verb;
    int idx = 0;
    while ((verb = iter.next(pts)) != pk::SkPath::kDone_Verb) {
      switch (verb) {
        case pk::SkPath::kMove_Verb:
          LOGI("[EllipseDump]   [%d] M (%g, %g)", idx, pts[0].fX, pts[0].fY);
          break;
        case pk::SkPath::kLine_Verb:
          LOGI("[EllipseDump]   [%d] L (%g, %g)", idx, pts[1].fX, pts[1].fY);
          break;
        case pk::SkPath::kQuad_Verb:
          LOGI("[EllipseDump]   [%d] Q ctrl=(%g, %g) end=(%g, %g)", idx,
               pts[1].fX, pts[1].fY, pts[2].fX, pts[2].fY);
          break;
        case pk::SkPath::kConic_Verb:
          LOGI("[EllipseDump]   [%d] C ctrl=(%g, %g) end=(%g, %g) w=%g", idx,
               pts[1].fX, pts[1].fY, pts[2].fX, pts[2].fY, iter.conicWeight());
          break;
        case pk::SkPath::kCubic_Verb:
          LOGI("[EllipseDump]   [%d] B ctrl1=(%g, %g) ctrl2=(%g, %g) end=(%g, %g)", idx,
               pts[1].fX, pts[1].fY, pts[2].fX, pts[2].fY, pts[3].fX, pts[3].fY);
          break;
        case pk::SkPath::kClose_Verb:
          LOGI("[EllipseDump]   [%d] Z", idx);
          break;
        default:
          break;
      }
      ++idx;
    }

    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
