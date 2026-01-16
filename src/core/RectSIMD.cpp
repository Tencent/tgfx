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

#include "tgfx/core/Rect.h"
// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/RectSIMD.cpp"
// Generates code for each enabled target by re-including this source file.
#include "hwy/foreach_target.h"  // IWYU pragma: keep

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
// Must come after foreach_target.h to avoid redefinition errors.
#include "hwy/highway.h"
#pragma clang diagnostic pop

HWY_BEFORE_NAMESPACE();
namespace tgfx {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
bool SetBoundsHWYImpl(Rect* rect, const Point* pts, int count) {
  if (count <= 0) {
    rect->setEmpty();
    return false;
  }
  const hn::Full128<float> d;
  hn::Vec<hn::Full128<float>> min, max;
  if (count & 1) {
    min = max = hn::Dup128VecFromValues(d, pts[0].x, pts[0].y, pts[0].x, pts[0].y);
    pts += 1;
    count -= 1;
  } else {
    min = max = hn::Dup128VecFromValues(d, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
    pts += 2;
    count -= 2;
  }
  auto accum = hn::Mul(min, hn::Set(d, 0.0f));
  while (count) {
    auto xy = hn::Dup128VecFromValues(d, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
    accum = hn::Mul(accum, xy);
    min = hn::Min(min, xy);
    max = hn::Max(max, xy);
    pts += 2;
    count -= 2;
  }
  auto mask = hn::Eq(hn::Mul(accum, hn::Set(d, 0.0f)), Set(d, 0.0f));
  hn::DFromM<decltype(mask)> md;
  const bool allFinite = hn::AllTrue(md, mask);
  if (allFinite) {
    float minArray[4] = {};
    float maxArray[4] = {};
    hn::Store(min, d, minArray);
    hn::Store(max, d, maxArray);
    rect->setLTRB(std::min(minArray[0], minArray[2]), std::min(minArray[1], minArray[3]),
                  std::max(maxArray[0], maxArray[2]), std::max(maxArray[1], maxArray[3]));
    return true;
  } else {
    rect->setEmpty();
    return false;
  }
}
}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {
HWY_EXPORT(SetBoundsHWYImpl);

bool Rect::setBounds(const Point pts[], int count) {
  return HWY_DYNAMIC_DISPATCH(SetBoundsHWYImpl)(this, pts, count);
}
}  // namespace tgfx
#endif
