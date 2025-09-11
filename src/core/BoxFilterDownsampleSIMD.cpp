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
#include "BoxFilterDownsampleSIMD.h"
#include "core/utils/Log.h"
// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/BoxFilterDownsampleSIMD.cpp"
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

int ResizeAreaFast4chx16SIMDFuncImpl(int srcStep, int dstStep, const uint8_t* srcData,
                                     uint8_t* dstData, int w, int padding, int scale,
                                     int shiftNum) {
  DEBUG_ASSERT(scale >= 4);
  DEBUG_ASSERT(scale <= 16);
  int dstX = 0;
  hn::Full128<uint8_t> du8;
  hn::Full128<uint16_t> du16;
  hn::Full64<uint16_t> duh16;
  hn::Full32<uint8_t> duh8;
  auto value = hn::Set(duh16, static_cast<uint16_t>(padding));
  size_t n = static_cast<size_t>(scale / 4);
  for (; dstX <= w - 4; dstX += 4, srcData += hn::Lanes(du8) * n, dstData += 4) {
    auto sum = hn::Set(du16, 0);
    for (size_t i = 0; i < n; i++) {
      for (size_t j = 0; j < static_cast<size_t>(scale); j++) {
        hn::Vec<decltype(du8)> data;
        if (static_cast<size_t>(srcStep) % (hn::Lanes(du8) * sizeof(uint8_t)) == 0) {
          data = hn::Load(du8, srcData + j * static_cast<size_t>(srcStep) + i * hn::Lanes(du8));
        } else {
          data = hn::LoadU(du8, srcData + j * static_cast<size_t>(srcStep) + i * hn::Lanes(du8));
        }
        auto low = hn::PromoteLowerTo(du16, data);
        auto high = hn::PromoteUpperTo(du16, data);
        sum = hn::Add(hn::Add(low, high), sum);
      }
    }
    auto vDst = hn::ShiftRightSame(
        hn::Add(hn::Add(hn::LowerHalf(duh16, sum), hn::UpperHalf(duh16, sum)), value), shiftNum);
    auto res = hn::DemoteTo(duh8, vDst);
    if (static_cast<size_t>(dstStep) % (hn::Lanes(duh8) * sizeof(uint8_t)) == 0) {
      hn::Store(res, duh8, dstData);
    } else {
      hn::StoreU(res, duh8, dstData);
    }
  }
  return dstX;
}

int ResizeAreaFast4chxNSIMDFuncImpl(int srcStep, int dstStep, const uint8_t* srcData,
                                    uint8_t* dstData, int w, int padding, int scale, int shiftNum) {
  DEBUG_ASSERT(scale >= 32);
  int dstX = 0;
  hn::Full128<uint8_t> du8;
  hn::Full32<uint8_t> duh8;
  hn::Full128<uint16_t> du16;
  hn::Full128<uint32_t> du32;
  auto value = hn::Set(du32, static_cast<uint32_t>(padding));
  size_t n = static_cast<size_t>(scale / 4);
  for (; dstX <= w - 4; dstX += 4, srcData += hn::Lanes(du8) * n, dstData += 4) {
    auto sum = hn::Set(du32, 0);
    for (size_t i = 0; i < n; i++) {
      for (size_t j = 0; j < static_cast<size_t>(scale); j++) {
        hn::Vec<decltype(du8)> data;
        if (static_cast<size_t>(srcStep) % (hn::Lanes(du8) * sizeof(uint8_t)) == 0) {
          data = hn::Load(du8, srcData + j * static_cast<size_t>(srcStep) + i * hn::Lanes(du8));
        } else {
          data = hn::LoadU(du8, srcData + j * static_cast<size_t>(srcStep) + i * hn::Lanes(du8));
        }
        auto low = hn::PromoteLowerTo(du16, data);
        auto high = hn::PromoteUpperTo(du16, data);
        auto value0 = hn::PromoteLowerTo(du32, low);
        auto value1 = hn::PromoteUpperTo(du32, low);
        auto value2 = hn::PromoteLowerTo(du32, high);
        auto value3 = hn::PromoteUpperTo(du32, high);
        sum = hn::Add(hn::Add(hn::Add(value0, value1), hn::Add(value2, value3)), sum);
      }
    }
    auto vDst = hn::ShiftRightSame(hn::Add(sum, value), shiftNum);
    auto res = hn::DemoteTo(duh8, vDst);
    if (static_cast<size_t>(dstStep) % (hn::Lanes(duh8) * sizeof(uint8_t)) == 0) {
      hn::Store(res, duh8, dstData);
    } else {
      hn::StoreU(res, duh8, dstData);
    }
  }
  return dstX;
}

int ResizeAreaFast1chxNSIMDFuncImpl(int srcStep, int, const uint8_t* srcData, uint8_t* dstData,
                                    int w, int padding, int scale, int shiftNum) {
  DEBUG_ASSERT(scale >= 16);
  int dstX = 0;
  hn::Full128<uint8_t> du8;
  hn::Full128<uint16_t> du16;
  hn::Full128<uint32_t> du32;
  size_t n = static_cast<size_t>(scale / 16);
  for (; dstX <= w - 1; dstX += 1, srcData += n * hn::Lanes(du8), dstData += 1) {
    auto sum = hn::Set(du32, 0);
    for (size_t i = 0; i < n; i++) {
      for (size_t j = 0; j < static_cast<size_t>(scale); j++) {
        hn::Vec<decltype(du8)> row;
        if (static_cast<size_t>(srcStep) % (hn::Lanes(du8) * sizeof(uint8_t)) == 0) {
          row = hn::Load(du8, srcData + j * static_cast<size_t>(srcStep) + i * hn::Lanes(du8));
        } else {
          row = hn::LoadU(du8, srcData + j * static_cast<size_t>(srcStep) + i * hn::Lanes(du8));
        }
        auto low = hn::PromoteLowerTo(du16, row);
        auto high = hn::PromoteUpperTo(du16, row);
        auto value0 = hn::PromoteLowerTo(du32, low);
        auto value1 = hn::PromoteUpperTo(du32, low);
        auto value2 = hn::PromoteLowerTo(du32, high);
        auto value3 = hn::PromoteUpperTo(du32, high);
        sum = hn::Add(hn::Add(hn::Add(value0, value1), hn::Add(value2, value3)), sum);
      }
    }
    auto res = (hn::ReduceSum(du32, sum) + static_cast<uint32_t>(padding)) >> shiftNum;
    dstData[0] = static_cast<uint8_t>(res);
  }
  return dstX;
}

int ResizeAreaFastx2SIMDFuncImpl(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                                 uint8_t* dstData, int w, int padding, int shiftNum) {

  int dstX = 0;
  const uint8_t* srcData0 = srcData;
  const uint8_t* srcData1 = srcData0 + srcStep;
  if (channelNum == 1) {
    hn::ScalableTag<uint8_t> du8;
    hn::ScalableTag<uint16_t> du16;
    const int n = static_cast<int>(hn::Lanes(du8));
    auto value = hn::Set(du16, static_cast<uint16_t>(padding));
    for (; dstX <= w - n; dstX += n, srcData0 += 2 * n, srcData1 += 2 * n, dstData += n) {
      hn::Vec<decltype(du8)> row00, row01, row10, row11;
      hn::LoadInterleaved2(du8, srcData0, row00, row01);
      hn::LoadInterleaved2(du8, srcData1, row10, row11);
      auto vDst00 = hn::Add(hn::PromoteLowerTo(du16, row00), hn::PromoteLowerTo(du16, row01));
      auto vDst10 = hn::Add(hn::PromoteLowerTo(du16, row10), hn::PromoteLowerTo(du16, row11));
      auto vDst0 = hn::ShiftRightSame(hn::Add(hn::Add(vDst00, vDst10), value), shiftNum);

      auto vDst01 = hn::Add(hn::PromoteUpperTo(du16, row00), hn::PromoteUpperTo(du16, row01));
      auto vDst11 = hn::Add(hn::PromoteUpperTo(du16, row10), hn::PromoteUpperTo(du16, row11));
      auto vDst1 = hn::ShiftRightSame(hn::Add(hn::Add(vDst01, vDst11), value), shiftNum);
      auto dstValue = hn::OrderedDemote2To(du8, vDst0, vDst1);
      if (static_cast<size_t>(dstStep) % (hn::Lanes(du8) * sizeof(uint8_t)) == 0) {
        hn::Store(dstValue, du8, dstData);
      } else {
        hn::StoreU(dstValue, du8, dstData);
      }
    }
  } else if (channelNum == 4) {
    hn::Full128<uint8_t> fix128Du8;
    hn::Full64<uint8_t> fix64Du8;
    hn::Full128<uint16_t> fix128Du16;
    hn::Full64<uint16_t> fix64Du16;
    auto fixValue = hn::Set(fix128Du16, static_cast<uint16_t>(padding));
    for (; dstX <= w - 8; dstX += 8, srcData0 += 16, srcData1 += 16, dstData += 8) {
      hn::Vec<decltype(fix128Du8)> vRow0, vRow1;
      if (static_cast<size_t>(srcStep) % (hn::Lanes(fix128Du8) * sizeof(uint8_t)) == 0) {
        vRow0 = hn::Load(fix128Du8, srcData0);
        vRow1 = hn::Load(fix128Du8, srcData1);
      } else {
        vRow0 = hn::LoadU(fix128Du8, srcData0);
        vRow1 = hn::LoadU(fix128Du8, srcData1);
      }
      auto vRow00 = hn::PromoteLowerTo(fix128Du16, vRow0);
      auto vRow01 = hn::PromoteUpperTo(fix128Du16, vRow0);
      auto vRow10 = hn::PromoteLowerTo(fix128Du16, vRow1);
      auto vRow11 = hn::PromoteUpperTo(fix128Du16, vRow1);
      auto vP0 = hn::Add(vRow00, vRow10);
      auto vDst0 = hn::Add(hn::LowerHalf(fix64Du16, vP0), hn::UpperHalf(fix64Du16, vP0));
      auto vP1 = hn::Add(vRow01, vRow11);
      auto vDst1 = hn::Add(hn::LowerHalf(fix64Du16, vP1), hn::UpperHalf(fix64Du16, vP1));
      auto vDst =
          hn::ShiftRightSame(hn::Add(hn::Combine(fix128Du16, vDst1, vDst0), fixValue), shiftNum);
      auto res = hn::DemoteTo(fix64Du8, vDst);
      if (static_cast<size_t>(dstStep) % (hn::Lanes(fix64Du8) * sizeof(uint8_t)) == 0) {
        hn::Store(res, fix64Du8, dstData);
      } else {
        hn::StoreU(res, fix64Du8, dstData);
      }
    }
  }
  return dstX;
}

int ResizeAreaFastx4SIMDFuncImpl(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                                 uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  int dstX = 0;
  if (channelNum == 1) {
    const uint8_t* srcData0 = srcData;
    const uint8_t* srcData1 = srcData0 + srcStep;
    const uint8_t* srcData2 = srcData1 + srcStep;
    const uint8_t* srcData3 = srcData2 + srcStep;
    hn::ScalableTag<uint8_t> du8;
    hn::ScalableTag<uint16_t> du16;
    const int n = static_cast<int>(hn::Lanes(du8));
    auto value = hn::Set(du16, static_cast<uint16_t>(padding));
    for (; dstX <= w - n; dstX += n, srcData0 += 4 * n, srcData1 += 4 * n, srcData2 += 4 * n,
                          srcData3 += 4 * n, dstData += n) {
      hn::Vec<decltype(du8)> row[4][4];
      hn::LoadInterleaved4(du8, srcData0, row[0][0], row[0][1], row[0][2], row[0][3]);
      hn::LoadInterleaved4(du8, srcData1, row[1][0], row[1][1], row[1][2], row[1][3]);
      hn::LoadInterleaved4(du8, srcData2, row[2][0], row[2][1], row[2][2], row[2][3]);
      hn::LoadInterleaved4(du8, srcData3, row[3][0], row[3][1], row[3][2], row[3][3]);
      auto vDst00 = hn::Add(
          hn::Add(hn::PromoteLowerTo(du16, row[0][0]), hn::PromoteLowerTo(du16, row[0][1])),
          hn::Add(hn::PromoteLowerTo(du16, row[0][2]), hn::PromoteLowerTo(du16, row[0][3])));
      auto vDst10 = hn::Add(
          hn::Add(hn::PromoteLowerTo(du16, row[1][0]), hn::PromoteLowerTo(du16, row[1][1])),
          hn::Add(hn::PromoteLowerTo(du16, row[1][2]), hn::PromoteLowerTo(du16, row[1][3])));
      auto vDst20 = hn::Add(
          hn::Add(hn::PromoteLowerTo(du16, row[2][0]), hn::PromoteLowerTo(du16, row[2][1])),
          hn::Add(hn::PromoteLowerTo(du16, row[2][2]), hn::PromoteLowerTo(du16, row[2][3])));
      auto vDst30 = hn::Add(
          hn::Add(hn::PromoteLowerTo(du16, row[3][0]), hn::PromoteLowerTo(du16, row[3][1])),
          hn::Add(hn::PromoteLowerTo(du16, row[3][2]), hn::PromoteLowerTo(du16, row[3][3])));
      auto vDst0 = hn::ShiftRightSame(
          hn::Add(hn::Add(hn::Add(vDst00, vDst10), hn::Add(vDst20, vDst30)), value), shiftNum);

      auto vDst01 = hn::Add(
          hn::Add(hn::PromoteUpperTo(du16, row[0][0]), hn::PromoteUpperTo(du16, row[0][1])),
          hn::Add(hn::PromoteUpperTo(du16, row[0][2]), hn::PromoteUpperTo(du16, row[0][3])));
      auto vDst11 = hn::Add(
          hn::Add(hn::PromoteUpperTo(du16, row[1][0]), hn::PromoteUpperTo(du16, row[1][1])),
          hn::Add(hn::PromoteUpperTo(du16, row[1][2]), hn::PromoteUpperTo(du16, row[1][3])));
      auto vDst21 = hn::Add(
          hn::Add(hn::PromoteUpperTo(du16, row[2][0]), hn::PromoteUpperTo(du16, row[2][1])),
          hn::Add(hn::PromoteUpperTo(du16, row[2][2]), hn::PromoteUpperTo(du16, row[2][3])));
      auto vDst31 = hn::Add(
          hn::Add(hn::PromoteUpperTo(du16, row[3][0]), hn::PromoteUpperTo(du16, row[3][1])),
          hn::Add(hn::PromoteUpperTo(du16, row[3][2]), hn::PromoteUpperTo(du16, row[3][3])));
      auto vDst1 = ShiftRightSame(
          hn::Add(hn::Add(hn::Add(vDst01, vDst11), hn::Add(vDst21, vDst31)), value), shiftNum);
      auto dstValue = hn::OrderedDemote2To(du8, vDst0, vDst1);
      if (static_cast<size_t>(dstStep) % (hn::Lanes(du8) * sizeof(uint8_t)) == 0) {
        hn::Store(dstValue, du8, dstData);
      } else {
        hn::StoreU(dstValue, du8, dstData);
      }
    }
  } else if (channelNum == 4) {
    dstX = ResizeAreaFast4chx16SIMDFuncImpl(srcStep, dstStep, srcData, dstData, w, padding, scale,
                                            shiftNum);
  }
  return dstX;
}

int ResizeAreaFastx8SIMDFuncImpl(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                                 uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  int dstX = 0;
  if (channelNum == 1) {
    hn::Full128<uint8_t> du8;
    hn::Full128<uint16_t> du16;
    for (; dstX <= w - 2; dstX += 2, srcData += 16, dstData += 2) {
      hn::Vec<decltype(du16)> sum0 = hn::Set(du16, 0), sum1 = hn::Set(du16, 0);
      for (int i = 0; i < 8; i++) {
        hn::Vec<decltype(du8)> row;
        if (static_cast<size_t>(srcStep) % (hn::Lanes(du8) * sizeof(uint8_t)) == 0) {
          row = hn::Load(du8, srcData + i * srcStep);
        } else {
          row = hn::LoadU(du8, srcData + i * srcStep);
        }
        sum0 = hn::Add(sum0, hn::PromoteLowerTo(du16, row));
        sum1 = hn::Add(sum1, hn::PromoteUpperTo(du16, row));
      }
      auto v0 = (hn::ReduceSum(du16, sum0) + padding) >> shiftNum;
      auto v1 = (hn::ReduceSum(du16, sum1) + padding) >> shiftNum;
      dstData[0] = static_cast<uint8_t>(v0);
      dstData[1] = static_cast<uint8_t>(v1);
    }
  } else if (channelNum == 4) {
    dstX = ResizeAreaFast4chx16SIMDFuncImpl(srcStep, dstStep, srcData, dstData, w, padding, scale,
                                            shiftNum);
  }
  return dstX;
}

int ResizeAreaFastx16SimdFuncImpl(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                                  uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  int dstX = 0;
  if (channelNum == 1) {
    dstX = ResizeAreaFast1chxNSIMDFuncImpl(srcStep, dstStep, srcData, dstData, w, padding, scale,
                                           shiftNum);
  } else if (channelNum == 4) {
    dstX = ResizeAreaFast4chx16SIMDFuncImpl(srcStep, dstStep, srcData, dstData, w, padding, scale,
                                            shiftNum);
  }
  return dstX;
}

int ResizeAreaFastxNSimdFuncImpl(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                                 uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  DEBUG_ASSERT(scale >= 32);
  int dstX = 0;
  if (channelNum == 1) {
    dstX = ResizeAreaFast1chxNSIMDFuncImpl(srcStep, dstStep, srcData, dstData, w, padding, scale,
                                           shiftNum);
  } else if (channelNum == 4) {
    dstX = ResizeAreaFast4chxNSIMDFuncImpl(srcStep, dstStep, srcData, dstData, w, padding, scale,
                                           shiftNum);
  }
  return dstX;
}

void MulImpl(const float* buf, int width, float beta, float* sum) {
  hn::ScalableTag<float> df;
  int size = width;
  int vecSize = size - size % static_cast<int>(hn::Lanes(df));
  for (int dstX = 0; dstX < vecSize; dstX += hn::Lanes(df)) {
    auto bufVec = hn::LoadU(df, &buf[dstX]);
    auto res = hn::Mul(bufVec, hn::Set(df, beta));
    hn::StoreU(res, df, &sum[dstX]);
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
    auto bufVec = hn::LoadU(df, &buf[dstX]);
    auto sumVec = hn::LoadU(df, &sum[dstX]);
    auto res = hn::Add(hn::Mul(bufVec, hn::Set(df, beta)), sumVec);
    hn::StoreU(res, df, &sum[dstX]);
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
HWY_EXPORT(ResizeAreaFastx2SIMDFuncImpl);
HWY_EXPORT(ResizeAreaFastx4SIMDFuncImpl);
HWY_EXPORT(ResizeAreaFastx8SIMDFuncImpl);
HWY_EXPORT(ResizeAreaFastx16SimdFuncImpl);
HWY_EXPORT(ResizeAreaFastxNSimdFuncImpl);
HWY_EXPORT(MulImpl);
HWY_EXPORT(MulAddImpl);

int ResizeAreaFastx2SIMDFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int, int padding, int shiftNum) {
  return HWY_DYNAMIC_DISPATCH(ResizeAreaFastx2SIMDFuncImpl)(channelNum, srcStep, dstStep, srcData,
                                                            dstData, w, padding, shiftNum);
}

int ResizeAreaFastx4SIMDFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  return HWY_DYNAMIC_DISPATCH(ResizeAreaFastx4SIMDFuncImpl)(channelNum, srcStep, dstStep, srcData,
                                                            dstData, w, scale, padding, shiftNum);
}

int ResizeAreaFastx8SIMDFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  return HWY_DYNAMIC_DISPATCH(ResizeAreaFastx8SIMDFuncImpl)(channelNum, srcStep, dstStep, srcData,
                                                            dstData, w, scale, padding, shiftNum);
}

int ResizeAreaFastx16SimdFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                              uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  return HWY_DYNAMIC_DISPATCH(ResizeAreaFastx16SimdFuncImpl)(channelNum, srcStep, dstStep, srcData,
                                                             dstData, w, scale, padding, shiftNum);
}

int ResizeAreaFastxNSimdFunc(int channelNum, int srcStep, int dstStep, const uint8_t* srcData,
                             uint8_t* dstData, int w, int scale, int padding, int shiftNum) {
  return HWY_DYNAMIC_DISPATCH(ResizeAreaFastxNSimdFuncImpl)(channelNum, srcStep, dstStep, srcData,
                                                            dstData, w, scale, padding, shiftNum);
}

void Mul(const float* buf, int width, float beta, float* sum) {
  return HWY_DYNAMIC_DISPATCH(MulImpl)(buf, width, beta, sum);
}

void MulAdd(const float* buf, int width, float beta, float* sum) {
  return HWY_DYNAMIC_DISPATCH(MulAddImpl)(buf, width, beta, sum);
}

}  // namespace tgfx
#endif