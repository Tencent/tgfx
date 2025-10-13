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

#include "FTRasterTarget.h"

namespace tgfx {
void GraySpanFunc(int y, int count, const FT_Span* spans, void* user) {
  auto target = reinterpret_cast<FTRasterTarget*>(user);
  for (int i = 0; i < count; i++) {
    auto q = target->origin - target->pitch * y + spans[i].x;
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
}  // namespace tgfx
