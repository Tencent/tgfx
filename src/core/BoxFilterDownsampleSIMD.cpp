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
int ResizeAreaFastSIMDFuncImpl(int channelNum, int step, const uint8_t* srcData, uint8_t* dstData, int w) {
  int dstX = 0;
  const uint8_t* srcData0 = srcData;
  const uint8_t* srcData1 = srcData0 + step;
  hn::ScalableTag<uint8_t> du8;
  hn::ScalableTag<uint16_t> du16;
  const int n = static_cast<int>(hn::Lanes(du8));
  auto value2 = hn::Set(du16, 2);
  if(channelNum == 1) {
    for(; dstX <= w - n; dstX += n, srcData0 += 2 * n, srcData1 += 2 * n, dstData += n) {
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
  }else if(channelNum == 4) {
    for(; dstX <= w - 2 * n; dstX += 2 * n, srcData0 += 4 * n, srcData1 += 4 * n, dstData += 2 * n) {
      hn::Vec<decltype(du8)> row0r, row0g, row0b, row0a, row1r, row1g, row1b, row1a;
      hn::LoadInterleaved4(du8, srcData0, row0r, row0g, row0b, row0a);
      hn::LoadInterleaved4(du8, srcData1, row1r, row1g, row1b, row1a);
      auto row0rSum = hn::SumsOf2(row0r);
      auto row1rSum = hn::SumsOf2(row1r);
      auto rSum = hn::DemoteTo(du8, hn::ShiftRight<2>(hn::Add(hn::Add(row0rSum, row1rSum), value2)));
      auto row0gSum = hn::SumsOf2(row0g);
      auto row1gSum = hn::SumsOf2(row1g);
      auto gSum = hn::DemoteTo(du8, hn::ShiftRight<2>(hn::Add(hn::Add(row0gSum, row1gSum), value2)));
      auto row0bSum = hn::SumsOf2(row0b);
      auto row1bSum = hn::SumsOf2(row1b);
      auto bSum = hn::DemoteTo(du8, hn::ShiftRight<2>(hn::Add(hn::Add(row0bSum, row1bSum), value2)));
      auto row0aSum = hn::SumsOf2(row0a);
      auto row1aSum = hn::SumsOf2(row1a);
      auto aSum = hn::DemoteTo(du8, hn::ShiftRight<2>(hn::Add(hn::Add(row0aSum, row1aSum), value2)));
      auto d = hn::DFromV<decltype(rSum)>();
      hn::StoreInterleaved4(rSum, gSum, bSum, aSum, d, dstData);
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
  for(int dstX = vecSize; dstX < size; dstX++) {
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
  for(int dstX = vecSize; dstX < size; dstX++) {
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

int ResizeAreaFastSIMDFunc(int channelNum, int step, const uint8_t* srcData, uint8_t* dstData, int w) {
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