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

#pragma once
#include <vector>
#include "tgfx/core/Rect.h"

namespace tgfx {

class OpaqueBoundsHelper {
 public:
  // Check if bounds is fully contained within any of the opaque bounds.
  static bool Contains(const std::vector<Rect>& opaqueBounds, const Rect& bounds);

  // Merge a new opaque bounds into the collection, maintaining at most 3 bounds sorted by area.
  static void Merge(std::vector<Rect>& opaqueBounds, const Rect& bounds);

 private:
  static Rect GetMaxOverlapRect(const Rect& rect1, const Rect& rect2);
};

}  // namespace tgfx
