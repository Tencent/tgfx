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

#include "BoxFilterDownsample.h"
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include "utils/Log.h"

namespace tgfx {

constexpr int ChannelSizeInBytes = 1;

struct DecimateAlpha {
  int srcIndex, dstIndex;
  float alpha;
};

struct FastFuncInfo {
  void* pixels;
  PixelLayout layout;
};

template <typename Tp>
static inline Tp SaturateCast(float v) {
  return static_cast<Tp>(v);
}
template <typename Tp>
static inline Tp SaturateCast(double v) {
  return static_cast<Tp>(v);
}
template <typename Tp>
static inline Tp SaturateCast(int v) {
  return static_cast<Tp>(v);
}

template <>
inline uint8_t SaturateCast<uint8_t>(int v) {
  return static_cast<uint8_t>(static_cast<unsigned>(v) <= UCHAR_MAX ? v : v > 0 ? UCHAR_MAX : 0);
}
template <>
inline uint8_t SaturateCast<uint8_t>(float v) {
  int iv = static_cast<int>(lrintf(v));
  return SaturateCast<uint8_t>(iv);
}
template <>
inline int SaturateCast<int>(double v) {
  return static_cast<int>(lrint(v));
}

static void SaturateStore(const float* sum, int width, uint8_t* dstData) {
  for (int dstX = 0; dstX < width; ++dstX) {
    dstData[dstX] = SaturateCast<uint8_t>(sum[dstX]);
  }
}

static void Mul(const float* buf, int width, float beta, float* sum) {
  for (int dstX = 0; dstX < width; ++dstX) {
    sum[dstX] = beta * buf[dstX];
  }
}

static void MulAdd(const float* buf, int width, float beta, float* sum) {
  for (int dstX = 0; dstX < width; ++dstX) {
    sum[dstX] += beta * buf[dstX];
  }
}

static int ComputeResizeAreaTab(int srcSize, int dstSize, int channelNum, double scale,
                                DecimateAlpha* tab) {
  int k = 0;
  for (int dstX = 0; dstX < dstSize; dstX++) {
    double fsx1 = dstX * scale;
    double fsx2 = fsx1 + scale;
    double cellWidth = std::min(scale, srcSize - fsx1);

    int sx1 = static_cast<int>(std::ceil(fsx1)), sx2 = static_cast<int>(std::floor(fsx2));

    sx2 = std::min(sx2, srcSize - 1);
    sx1 = std::min(sx1, sx2);

    if (sx1 - fsx1 > 1e-3) {
      ASSERT(k < srcSize * 2);
      tab[k].dstIndex = dstX * channelNum;
      tab[k].srcIndex = (sx1 - 1) * channelNum;
      tab[k++].alpha = static_cast<float>((sx1 - fsx1) / cellWidth);
    }

    for (int sx = sx1; sx < sx2; sx++) {
      ASSERT(k < srcSize * 2);
      tab[k].dstIndex = dstX * channelNum;
      tab[k].srcIndex = sx * channelNum;
      tab[k++].alpha = static_cast<float>(1.0 / cellWidth);
    }

    if (fsx2 - sx2 > 1e-3) {
      ASSERT(k < srcSize * 2);
      tab[k].dstIndex = dstX * channelNum;
      tab[k].srcIndex = sx2 * channelNum;
      tab[k++].alpha =
          static_cast<float>(std::min(std::min(fsx2 - sx2, 1.), cellWidth) / cellWidth);
    }
  }
  return k;
}

static void ResizeAreaFast(const FastFuncInfo& srcInfo, FastFuncInfo& dstInfo, const int* offset,
                           const int* xOffset, int scaleX, int scaleY, int channelNum) {
  int area = scaleX * scaleY;
  float scale = 1.f / static_cast<float>(area);
  int dwith1 = (srcInfo.layout.width / scaleX) * channelNum;
  int dstWidth = dstInfo.layout.width * channelNum;
  int srcWidth = srcInfo.layout.width * channelNum;
  int dstY, dstX, k = 0;

  for (dstY = 0; dstY < dstInfo.layout.height; dstY++) {
    auto* dstData = static_cast<uint8_t*>(dstInfo.pixels) + dstY * dstInfo.layout.rowBytes;
    int srcY0 = dstY * scaleY;
    int w = srcY0 + scaleY <= srcInfo.layout.height ? dwith1 : 0;
    if (srcY0 >= srcInfo.layout.height) {
      for (dstX = 0; dstX < dstWidth; dstX++) {
        dstData[dstX] = 0;
      }
      continue;
    }

    for (dstX = 0; dstX < w; dstX++) {
      const uint8_t* srcData =
          static_cast<uint8_t*>(srcInfo.pixels) + srcY0 * srcInfo.layout.rowBytes + xOffset[dstX];
      int sum = 0;
      for (k = 0; k < area; k++) {
        sum += srcData[offset[k]];
      }
      dstData[dstX] = SaturateCast<uint8_t>(static_cast<float>(sum) * scale);
    }

    for (; dstX < dstWidth; dstX++) {
      int sum = 0;
      int count = 0, srcX0 = xOffset[dstX];
      if (srcX0 >= srcWidth) {
        dstData[dstX] = 0;
      }
      for (int srcY = 0; srcY < scaleY; srcY++) {
        if (srcY0 + srcY >= srcInfo.layout.height) {
          break;
        }
        const uint8_t* srcData = static_cast<uint8_t*>(srcInfo.pixels) +
                                 (srcY0 + srcY) * srcInfo.layout.rowBytes + srcX0;
        for (int srcX = 0; srcX < scaleX * channelNum; srcX += channelNum) {
          if (srcX0 + srcX >= srcWidth) {
            break;
          }
          sum += srcData[srcX];
          count++;
        }
      }
      dstData[dstX] = SaturateCast<uint8_t>(static_cast<float>(sum) / static_cast<float>(count));
    }
  }
}

static void ResizeArea(const FastFuncInfo& srcInfo, FastFuncInfo& dstInfo,
                       const DecimateAlpha* xTab, int xTabSize, const DecimateAlpha* yTab, int,
                       const int* tabOffset, int channelNum) {
  int dstWidth = dstInfo.layout.width * channelNum;
  std::unique_ptr<float[]> _buffer(new float[static_cast<size_t>(dstWidth * 2)]);
  float* buf = _buffer.get();
  float* sum = buf + dstWidth;
  int jStart = tabOffset[0], jEnd = tabOffset[dstInfo.layout.height], j, k, dstX,
      prevDstY = yTab[jStart].dstIndex;

  for (dstX = 0; dstX < dstWidth; dstX++) {
    sum[dstX] = 0.0f;
  }
  for (j = jStart; j < jEnd; j++) {
    float beta = yTab[j].alpha;
    int dstY = yTab[j].dstIndex;
    int srcY = yTab[j].srcIndex;
    {
      const uint8_t* srcData =
          static_cast<uint8_t*>(srcInfo.pixels) + srcY * srcInfo.layout.rowBytes;
      for (dstX = 0; dstX < dstWidth; dstX++) {
        buf[dstX] = 0.0f;
      }
      if (channelNum == 1) {
        for (k = 0; k < xTabSize; k++) {
          int dxn = xTab[k].dstIndex;
          float alpha = xTab[k].alpha;
          buf[dxn] += static_cast<float>(srcData[xTab[k].srcIndex]) * alpha;
        }
      } else {
        for (k = 0; k < xTabSize; k++) {
          int sxn = xTab[k].srcIndex;
          int dxn = xTab[k].dstIndex;
          float alpha = xTab[k].alpha;
          float t0 = buf[dxn] + srcData[sxn] * alpha;
          float t1 = buf[dxn + 1] + srcData[sxn + 1] * alpha;
          buf[dxn] = t0;
          buf[dxn + 1] = t1;
          t0 = buf[dxn + 2] + srcData[sxn + 2] * alpha;
          t1 = buf[dxn + 3] + srcData[sxn + 3] * alpha;
          buf[dxn + 2] = t0;
          buf[dxn + 3] = t1;
        }
      }
    }

    if (dstY != prevDstY) {
      SaturateStore(sum, dstWidth,
                    static_cast<uint8_t*>(dstInfo.pixels) + prevDstY * dstInfo.layout.rowBytes);
      Mul(buf, dstWidth, beta, sum);
      prevDstY = dstY;
    } else {
      MulAdd(buf, dstWidth, beta, sum);
    }
  }
  SaturateStore(sum, dstWidth,
                static_cast<uint8_t*>(dstInfo.pixels) + prevDstY * dstInfo.layout.rowBytes);
}

void BoxFilterDownSample(const void* inputPixels, const PixelLayout& inputLayout,
                         void* outputPixels, const PixelLayout& outputLayout, bool alphaOnly) {
  ASSERT(inputPixels != nullptr && outputPixels != nullptr)
  ASSERT(inputLayout.width > 0 && inputLayout.height > 0 && outputLayout.width > 0 &&
         outputLayout.height > 0)
  ASSERT(inputLayout.rowBytes > 0 && outputLayout.rowBytes > 0)
  ASSERT(outputLayout.width <= inputLayout.width && outputLayout.height <= inputLayout.height)

  auto scaleX = static_cast<double>(inputLayout.width) / outputLayout.width;
  auto scaleY = static_cast<double>(inputLayout.height) / outputLayout.height;
  int channelNum = alphaOnly ? 1 : 4;
  int iScaleX = SaturateCast<int>(scaleX);
  int iScaleY = SaturateCast<int>(scaleY);
  bool isAreaFast =
      std::abs(scaleX - iScaleX) < DBL_EPSILON && std::abs(scaleY - iScaleY) < DBL_EPSILON;

  FastFuncInfo srcInfo = {};
  srcInfo.pixels = const_cast<void*>(inputPixels);
  srcInfo.layout = inputLayout;

  FastFuncInfo dstInfo = {};
  dstInfo.pixels = outputPixels;
  dstInfo.layout = outputLayout;

  int k, srcX, srcY, dstX, dstY;

  if (isAreaFast) {
    int area = iScaleX * iScaleY;
    size_t srcStep = static_cast<size_t>(inputLayout.rowBytes / ChannelSizeInBytes);
    std::unique_ptr<int[]> data(
        new int[static_cast<size_t>(area + outputLayout.width * channelNum)]);
    int* offset = data.get();
    int* xOffset = offset + area;

    for (srcY = 0, k = 0; srcY < iScaleY; srcY++) {
      for (srcX = 0; srcX < iScaleX; srcX++) {
        offset[k++] = static_cast<int>(static_cast<size_t>(srcY) * srcStep +
                                       static_cast<size_t>(srcX * channelNum));
      }
    }

    for (dstX = 0; dstX < outputLayout.width; dstX++) {
      int j = dstX * channelNum;
      srcX = iScaleX * j;
      for (k = 0; k < channelNum; k++) {
        xOffset[j + k] = srcX + k;
      }
    }

    ResizeAreaFast(srcInfo, dstInfo, offset, xOffset, iScaleX, iScaleY, channelNum);
    return;
  }

  std::unique_ptr<DecimateAlpha[]> _xytab(
      new DecimateAlpha[static_cast<size_t>((inputLayout.width + inputLayout.height) * 2)]);
  DecimateAlpha* xtab = _xytab.get();
  DecimateAlpha* ytab = xtab + inputLayout.width * 2;
  int xtabSize =
      ComputeResizeAreaTab(inputLayout.width, outputLayout.width, channelNum, scaleX, xtab);
  int ytabSize = ComputeResizeAreaTab(inputLayout.height, outputLayout.height, 1, scaleY, ytab);
  std::unique_ptr<int[]> _tabOffset(new int[static_cast<size_t>(outputLayout.height + 1)]);
  int* tabOffset = _tabOffset.get();
  for (k = 0, dstY = 0; k < ytabSize; k++) {
    if (k == 0 || ytab[k].dstIndex != ytab[k - 1].dstIndex) {
      ASSERT(ytab[k].dstIndex == dstY);
      tabOffset[dstY++] = k;
    }
  }
  tabOffset[dstY] = ytabSize;
  ResizeArea(srcInfo, dstInfo, xtab, xtabSize, ytab, ytabSize, tabOffset, channelNum);
}
}  // namespace tgfx