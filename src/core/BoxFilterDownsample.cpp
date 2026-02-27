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
#include <functional>
#include <memory>
#include "BoxFilterDownsampleSIMD.h"
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

/**
 * Computes the resize area table for downsampling an image channel.
 *
 * This function generates a table (tab) that maps each destination pixel to one or more source
 * pixels, along with blending weights (alpha) that determine how much each source pixel contributes
 * to the destination pixel. The weights are calculated based on the area of overlap between source
 * and destination pixels.
 *
 * @param srcSize     The size (width or height) of the source image dimension being processed.
 * @param dstSize     The size (width or height) of the destination image dimension.
 * @param channelNum  The number of channels in the image (1 for grayscale, 4 for RGBA).
 * @param scale       The scaling factor (srcSize/dstSize) for this dimension.
 * @param tab         Output array of DecimateAlpha structs that will store the mapping between
 *                    source and destination pixels, along with blending weights.
 *
 * @return            The total number of entries written to the tab array.
 *
 */
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

using ResizeFunc = int (*)(int, int, int, const uint8_t*, uint8_t*, int, int, int, int);

static bool IsPowerOfTwo(int n) {
  return (n > 0) && ((n & (n - 1)) == 0);
}

static int GetPowerOfTwo(int n) {
  if (n <= 0 || (n & (n - 1)) != 0) {
    return -1;
  }
  int k = 0;
  while (n >>= 1) {
    k++;
  }
  return k;
}

struct ResizeAreaFastVec {
  ResizeAreaFastVec(int scaleX, int scaleY, int channelNum, int srcStep, int dstStep)
      : scaleX(scaleX), scaleY(scaleY), channelNum(channelNum), srcStep(srcStep), dstStep(dstStep) {
    fastMode =
        this->scaleX == this->scaleY && IsPowerOfTwo(this->scaleX) && IsPowerOfTwo(this->scaleY);
    padding = scaleX * scaleY / 2;
    shiftNum = GetPowerOfTwo(scaleX) * 2;
    switch (this->scaleX) {
      case 2:
        resizeFunc = ResizeAreaFastx2SIMDFunc;
        break;
      case 4:
        resizeFunc = ResizeAreaFastx4SIMDFunc;
        break;
      case 8:
        resizeFunc = ResizeAreaFastx8SIMDFunc;
        break;
      case 16:
        resizeFunc = ResizeAreaFastx16SimdFunc;
        break;
      default:
        resizeFunc = ResizeAreaFastxNSimdFunc;
    }
  }

  int operator()(const uint8_t* srcData, uint8_t* dstData, int w) const {
    int dstX = 0;
    if (fastMode) {
      dstX =
          resizeFunc(channelNum, srcStep, dstStep, srcData, dstData, w, scaleX, padding, shiftNum);
      if (channelNum == 1) {
        for (; dstX < w; ++dstX) {
          int index = dstX * scaleX;
          int sum = 0;
          for (int i = 0; i < scaleY; i++) {
            for (int j = 0; j < scaleX; j++) {
              const uint8_t* data = srcData + srcStep * i;
              sum += data[index + j];
            }
          }
          dstData[dstX] = static_cast<uint8_t>((sum + padding) >> shiftNum);
        }
      } else {
        ASSERT(channelNum == 4);
        for (; dstX < w; dstX += 4) {
          int index = dstX * scaleX;
          int sum[4] = {0};
          for (int i = 0; i < scaleY; i++) {
            for (int j = 0; j < scaleX; j++) {
              const uint8_t* data = srcData + srcStep * i;
              sum[0] += data[index + 4 * j];
              sum[1] += data[index + 4 * j + 1];
              sum[2] += data[index + 4 * j + 2];
              sum[3] += data[index + 4 * j + 3];
            }
          }
          dstData[dstX] = static_cast<uint8_t>((sum[0] + padding) >> shiftNum);
          dstData[dstX + 1] = static_cast<uint8_t>((sum[1] + padding) >> shiftNum);
          dstData[dstX + 2] = static_cast<uint8_t>((sum[2] + padding) >> shiftNum);
          dstData[dstX + 3] = static_cast<uint8_t>((sum[3] + padding) >> shiftNum);
        }
      }
    }
    return dstX;
  }

 private:
  int scaleX = 0;
  int scaleY = 0;
  int channelNum = 0;
  bool fastMode = false;
  int srcStep = 0;
  int dstStep = 0;
  ResizeFunc resizeFunc = nullptr;
  int padding = 0;
  int shiftNum = 0;
};

/**
 * Performs fast area-based downsampling when the scaling factor is an exact integer ratio.
 *
 * This optimized version is used when both horizontal and vertical scaling factors are integers
 * (e.g., 1/2, 1/3). It works by averaging pixel values in fixed-size blocks from the source image
 * to produce each destination pixel.
 *
 * @param srcInfo    Contains source image data (pixels) and layout information
 * @param dstInfo    Contains destination image buffer and layout information
 * @param offset     Precomputed array of offsets for accessing source pixel blocks
 * @param xOffset    Precomputed horizontal offsets for each destination pixel column
 * @param scaleX     Horizontal scaling factor (must be integer)
 * @param scaleY     Vertical scaling factor (must be integer)
 * @param channelNum Number of channels (1 for grayscale, 4 for RGBA)
 *
 */
static void ResizeAreaFast(const FastFuncInfo& srcInfo, FastFuncInfo& dstInfo, const int* offset,
                           const int* xOffset, int scaleX, int scaleY, int channelNum) {
  int area = scaleX * scaleY;
  float scale = 1.f / static_cast<float>(area);
  int dwith1 = (srcInfo.layout.width / scaleX) * channelNum;
  int dstWidth = dstInfo.layout.width * channelNum;
  int srcWidth = srcInfo.layout.width * channelNum;
  int dstY, dstX, k = 0;
  ResizeAreaFastVec vecOp(scaleX, scaleY, channelNum, srcInfo.layout.rowBytes,
                          dstInfo.layout.rowBytes);
  for (dstY = 0; dstY < dstInfo.layout.height; dstY++) {
    auto dstData = static_cast<uint8_t*>(dstInfo.pixels) + dstY * dstInfo.layout.rowBytes;
    int srcY0 = dstY * scaleY;
    int w = srcY0 + scaleY <= srcInfo.layout.height ? dwith1 : 0;
    if (srcY0 >= srcInfo.layout.height) {
      for (dstX = 0; dstX < dstWidth; dstX++) {
        dstData[dstX] = 0;
      }
      continue;
    }
    dstX =
        vecOp(static_cast<uint8_t*>(srcInfo.pixels) + srcY0 * srcInfo.layout.rowBytes, dstData, w);
    for (; dstX < w; dstX++) {
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

/**
 * Performs generic area-based image downsampling for arbitrary scaling ratios.
 *
 * This function handles non-integer scaling factors by using weighted averaging of source pixels,
 * where weights are proportional to the area of overlap between source and destination pixels.
 * It supports both single-channel and multi-channel (RGBA) images.
 *
 * @param srcInfo    Source image information (pixel data and layout)
 * @param dstInfo    Destination image buffer and layout
 * @param xTab       Precomputed horizontal resizing table containing:
 *                   - srcIndex: source pixel index
 *                   - dstIndex: destination pixel index
 *                   - alpha: blending weight for this pixel
 * @param xTabSize   Number of entries in xTab
 * @param yTab       Precomputed vertical resizing table (same structure as xTab)
 * @param tabOffset  Offsets into yTab for each destination row
 * @param channelNum Number of color channels (1 or 4)
 *
 */
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

void BoxFilterDownsample(const void* inputPixels, const PixelLayout& inputLayout,
                         void* outputPixels, const PixelLayout& outputLayout, bool isOneComponent) {
  ASSERT(inputPixels != nullptr && outputPixels != nullptr)
  ASSERT(inputLayout.width > 0 && inputLayout.height > 0 && outputLayout.width > 0 &&
         outputLayout.height > 0)
  ASSERT(inputLayout.rowBytes > 0 && outputLayout.rowBytes > 0)
  ASSERT(outputLayout.width <= inputLayout.width && outputLayout.height <= inputLayout.height)

  auto scaleX = static_cast<double>(inputLayout.width) / outputLayout.width;
  auto scaleY = static_cast<double>(inputLayout.height) / outputLayout.height;
  int channelNum = isOneComponent ? 1 : 4;
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
