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

#include "DashEffect.h"
#include "core/utils/StrokeUtils.h"

namespace tgfx {

std::shared_ptr<PathEffect> CreateDashPathEffect(const std::vector<float>& dashes, float dashOffset,
                                                 bool adaptive, const Stroke& stroke) {
  if (dashes.empty()) {
    return nullptr;
  }
  std::vector<float> dashList = dashes;
  if (dashes.size() % 2 != 0) {
    dashList.insert(dashList.end(), dashes.begin(), dashes.end());
  }
  dashList = SimplifyLineDashPattern(dashList, stroke);
  if (dashList.empty()) {
    return nullptr;
  }
  return PathEffect::MakeDash(dashList.data(), static_cast<int>(dashList.size()), dashOffset,
                              adaptive);
}

}  // namespace tgfx
