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

#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Pixmap.h"
// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "gpu/GradientCacheSIMD.cpp"
// Generates code for each enabled target by re-including this source file.
#include "hwy/foreach_target.h"  // IWYU pragma: keep

// Must come after foreach_target.h to avoid redefinition errors.
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace tgfx {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
std::shared_ptr<ImageBuffer> CreateGradientImpl(const Color* colors, const float* positions,
                                                int count, int resolution) {
  Bitmap bitmap(resolution, 1, false, false);
  Pixmap pixmap(bitmap);
  if (pixmap.isEmpty()) {
    return nullptr;
  }
  pixmap.clear();
  auto pixels = reinterpret_cast<uint8_t*>(pixmap.writablePixels());
  int prevIndex = 0;
  const hn::Full128<float> df;
  const hn::Full128<uint32_t> du;
  const hn::Full32<uint8_t> du8;
  for (int i = 1; i < count; ++i) {
    int nextIndex =
        std::min(static_cast<int>(positions[i] * static_cast<float>(resolution)), resolution - 1);

    if (nextIndex > prevIndex) {
      auto color0 = hn::LoadU(df, &colors[i - 1].red);
      auto color1 = hn::LoadU(df, &colors[i].red);

      auto step = 1.0f / static_cast<float>(nextIndex - prevIndex);
      auto delta = hn::Mul(hn::Sub(color1, color0), hn::Set(df, step));

      for (int curIndex = prevIndex; curIndex <= nextIndex; ++curIndex) {
        auto res = hn::U8FromU32(hn::ConvertTo(du, hn::Mul(color0, hn::Set(df, 255.0f))));
        hn::StoreU(res, du8, &pixels[curIndex * 4]);
        color0 = hn::Add(color0, delta);
      }
    }
    prevIndex = nextIndex;
  }
  return bitmap.makeBuffer();
}

}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {
HWY_EXPORT(CreateGradientImpl);

std::shared_ptr<ImageBuffer> CreateGradient(const Color* colors, const float* positions,
                                               int count, int resolution) {
  return HWY_DYNAMIC_DISPATCH(CreateGradientImpl)(colors, positions, count, resolution);
}
}  // namespace tgfx
#endif