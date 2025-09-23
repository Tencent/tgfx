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

#include "TileSortCompareFunc.h"

// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/utils/TileSortCompareFuncSIMD.cpp"
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
bool TileSortCompareFuncImpl(const Point& center, float tileSize, const std::pair<int, int>& a,
                             const std::pair<int, int>& b, SortOrder order) {
  const hn::Full128<int32_t> di;
  const hn::Full128<float> df;
  auto res =
      hn::Add(hn::ConvertTo(df, hn::Dup128VecFromValues(di, a.first, a.second, b.first, b.second)),
              hn::Set(df, 0.5f));
  res = hn::MulSub(res, hn::Set(df, tileSize),
                   hn::Dup128VecFromValues(df, center.x, center.y, center.x, center.y));
  res = hn::Mul(res, res);
  res = hn::Add(hn::Reverse2(df, res), res);
  float resVec[4] = {0.0f};
  hn::Store(res, df, resVec);
  return (order == SortOrder::Ascending) ? resVec[0] < resVec[2] : resVec[0] > resVec[2];
}
}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {
HWY_EXPORT(TileSortCompareFuncImpl);

bool TileSortCompareFunc(const Point& center, float tileSize, const std::pair<int, int>& a,
                         const std::pair<int, int>& b, SortOrder order) {
  return HWY_DYNAMIC_DISPATCH(TileSortCompareFuncImpl)(center, tileSize, a, b, order);
}
}  // namespace tgfx
#endif
