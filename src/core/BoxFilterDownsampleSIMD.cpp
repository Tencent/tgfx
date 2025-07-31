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

#include <cstdint>
// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/BoxFilterDownsampleSIMD.cpp"
// Generates code for each enabled target by re-including this source file.
#include "hwy/foreach_target.h"  // IWYU pragma: keep

// Must come after foreach_target.h to avoid redefinition errors.
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace tgfx {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
int ResizeAreaFastSIMDFuncImpl(int channelNum, int step, const uint8_t* srcData, uint8_t* dstData,
                               int w) {
  int dstX = 0;
  const uint8_t* srcData0 = srcData;
  const uint8_t* srcData1 = srcData0 + step;
  hn::ScalableTag<uint8_t> du8;
  hn::ScalableTag<uint16_t> du16;
  const int n = static_cast<int>(hn::Lanes(du8));
  auto value2 = hn::Set(du16, 2);
  if (channelNum == 1) {
    for (; dstX <= w - n; dstX += n, srcData0 += 2 * n, srcData1 += 2 * n, dstData += n) {
      hn::Vec<decltype(du8)> row00, row01, row10, row11;
      hn::LoadInterleaved2(du8, srcData0, row00, row01);
      hn::LoadInterleaved2(du8, srcData1, row10, row11);
      auto vDst00 = hn::Add(hn::PromoteLowerTo(du16, row00), hn::PromoteLowerTo(du16, row01));
      auto vDst10 = hn::Add(hn::PromoteLowerTo(du16, row10), hn::PromoteLowerTo(du16, row11));
      auto vDst0 = hn::ShiftRight<2>(hn::Add(hn::Add(vDst00, vDst10), value2));

      auto vDst01 = hn::Add(hn::PromoteUpperTo(du16, row00), hn::PromoteUpperTo(du16, row01));
      auto vDst11 = hn::Add(hn::PromoteUpperTo(du16, row10), hn::PromoteUpperTo(du16, row11));
      auto vDst1 = hn::ShiftRight<2>(hn::Add(hn::Add(vDst01, vDst11), value2));
      auto dstValue = hn::OrderedDemote2To(du8, vDst0, vDst1);
      hn::Store(dstValue, du8, dstData);
    }
  } else if (channelNum == 4) {
    hn::Full128<uint8_t> fix128Du8;
    hn::Full64<uint8_t> fix64Du8;
    hn::Full128<uint16_t> fix128Du16;
    hn::Full64<uint16_t> fix64Du16;
    auto fixValue2 = hn::Set(fix128Du16, 2);
    for (; dstX <= w - 8; dstX += 8, srcData0 += 16, srcData1 += 16, dstData += 8) {
      auto vRow0 = hn::Load(fix128Du8, srcData0);
      auto vRow1 = hn::Load(fix128Du8, srcData1);
      auto vRow00 = hn::PromoteLowerTo(fix128Du16, vRow0);
      auto vRow01 = hn::PromoteUpperTo(fix128Du16, vRow0);
      auto vRow10 = hn::PromoteLowerTo(fix128Du16, vRow1);
      auto vRow11 = hn::PromoteUpperTo(fix128Du16, vRow1);
      auto vP0 =
          hn::Add(hn::Add(hn::LowerHalf(fix64Du16, vRow00), hn::UpperHalf(fix64Du16, vRow00)),
                  hn::Add(hn::LowerHalf(fix64Du16, vRow10), hn::UpperHalf(fix64Du16, vRow10)));
      auto vP1 =
          hn::Add(hn::Add(hn::LowerHalf(fix64Du16, vRow01), hn::UpperHalf(fix64Du16, vRow01)),
                  hn::Add(hn::LowerHalf(fix64Du16, vRow11), hn::UpperHalf(fix64Du16, vRow11)));
      auto vDst = hn::ShiftRight<2>(hn::Add(hn::Combine(fix128Du16, vP1, vP0), fixValue2));
      auto res = hn::DemoteTo(fix64Du8, vDst);
      hn::Store(res, fix64Du8, dstData);
    }
  }
  return dstX;
}

void MulImpl(const float* buf, int width, float beta, float* sum) {
  hn::ScalableTag<float> df;
  int size = width;
  int vecSize = size - size % static_cast<int>(hn::Lanes(df));
  for (int dstX = 0; dstX < vecSize; dstX += hn::Lanes(df)) {
    auto bufVec = hn::Load(df, &buf[dstX]);
    auto res = hn::Mul(bufVec, hn::Set(df, beta));
    hn::Store(res, df, &sum[dstX]);
  }
  for (int dstX = vecSize; dstX < size; dstX++) {
    sum[dstX] = beta * buf[dstX];
  }
}

void MulAddImpl(const float* buf, int width, float beta, float* sum) {
  hn::ScalableTag<float> df;
  int size = width;
  int vecSize = size - size % static_cast<int>(hn::Lanes(df));
  for (int dstX = 0; dstX < vecSize; dstX += hn::Lanes(df)) {
    auto bufVec = hn::Load(df, &buf[dstX]);
    auto sumVec = hn::Load(df, &sum[dstX]);
    auto res = hn::Add(hn::Mul(bufVec, hn::Set(df, beta)), sumVec);
    hn::Store(res, df, &sum[dstX]);
  }
  for (int dstX = vecSize; dstX < size; dstX++) {
    sum[dstX] += beta * buf[dstX];
  }
}
}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {
HWY_EXPORT(ResizeAreaFastSIMDFuncImpl);
HWY_EXPORT(MulImpl);
HWY_EXPORT(MulAddImpl);

int ResizeAreaFastSIMDFunc(int channelNum, int step, const uint8_t* srcData, uint8_t* dstData,
                           int w) {
  return HWY_DYNAMIC_DISPATCH(ResizeAreaFastSIMDFuncImpl)(channelNum, step, srcData, dstData, w);
}

void Mul(const float* buf, int width, float beta, float* sum) {
  return HWY_DYNAMIC_DISPATCH(MulImpl)(buf, width, beta, sum);
}

void MulAdd(const float* buf, int width, float beta, float* sum) {
  return HWY_DYNAMIC_DISPATCH(MulAddImpl)(buf, width, beta, sum);
}

}  // namespace tgfx
#endif