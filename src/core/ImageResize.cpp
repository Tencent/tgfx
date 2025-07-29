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

#include "ImageResize.h"
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
  int si, di;
  float alpha;
};

struct FastFuncInfo {
  void* pixels;
  int w;
  int h;
  int strideInBytes;
};

template <typename _Tp>
static inline _Tp saturateCast(float v) {
  return _Tp(v);
}
template <typename _Tp>
static inline _Tp saturateCast(double v) {
  return _Tp(v);
}
template <typename _Tp>
static inline _Tp saturateCast(int v) {
  return _Tp(v);
}

template <>
inline uint8_t saturateCast<uint8_t>(int v) {
  return (uint8_t)((unsigned)v <= UCHAR_MAX ? v : v > 0 ? UCHAR_MAX : 0);
}
template <>
inline uint8_t saturateCast<uint8_t>(float v) {
  int iv = (int)lrintf(v);
  return saturateCast<uint8_t>(iv);
}
template <>
inline int saturateCast<int>(double v) {
  return (int)lrint(v);
}

static void saturateStore(const float* sum, int width, uint8_t* D) {
  for (int dx = 0; dx < width; ++dx) {
    D[dx] = saturateCast<uint8_t>(sum[dx]);
  }
}

static void mul(const float* buf, int width, float beta, float* sum) {
  for (int dx = 0; dx < width; ++dx) {
    sum[dx] = beta * buf[dx];
  }
}

static void mulAdd(const float* buf, int width, float beta, float* sum) {
  for (int dx = 0; dx < width; ++dx) {
    sum[dx] += beta * buf[dx];
  }
}

static int computeResizeAreaTab(int ssize, int dsize, int cn, double scale, DecimateAlpha* tab) {
  int k = 0;
  for (int dx = 0; dx < dsize; dx++) {
    double fsx1 = dx * scale;
    double fsx2 = fsx1 + scale;
    double cellWidth = std::min(scale, ssize - fsx1);

    int sx1 = static_cast<int>(std::ceil(fsx1)), sx2 = static_cast<int>(std::floor(fsx2));

    sx2 = std::min(sx2, ssize - 1);
    sx1 = std::min(sx1, sx2);

    if (sx1 - fsx1 > 1e-3) {
      ASSERT(k < ssize * 2);
      tab[k].di = dx * cn;
      tab[k].si = (sx1 - 1) * cn;
      tab[k++].alpha = (float)((sx1 - fsx1) / cellWidth);
    }

    for (int sx = sx1; sx < sx2; sx++) {
      ASSERT(k < ssize * 2);
      tab[k].di = dx * cn;
      tab[k].si = sx * cn;
      tab[k++].alpha = float(1.0 / cellWidth);
    }

    if (fsx2 - sx2 > 1e-3) {
      ASSERT(k < ssize * 2);
      tab[k].di = dx * cn;
      tab[k].si = sx2 * cn;
      tab[k++].alpha = (float)(std::min(std::min(fsx2 - sx2, 1.), cellWidth) / cellWidth);
    }
  }
  return k;
}

static void ResizeAreaFast(const FastFuncInfo& srcInfo, FastFuncInfo& dstInfo, const int* ofs,
                           const int* xofs, int scaleX, int scaleY, int cn) {
  int area = scaleX * scaleY;
  float scale = 1.f / static_cast<float>(area);
  int dwith1 = (srcInfo.w / scaleX) * cn;
  int dstWidth = dstInfo.w * cn;
  int srcWidth = srcInfo.w * cn;
  int dy, dx, k = 0;

  for (dy = 0; dy < dstInfo.h; dy++) {
    auto* D = (uint8_t*)dstInfo.pixels + dy * dstInfo.strideInBytes;
    int sy0 = dy * scaleY;
    int w = sy0 + scaleY <= srcInfo.h ? dwith1 : 0;
    if (sy0 >= srcInfo.h) {
      for (dx = 0; dx < dstWidth; dx++) {
        D[dx] = 0;
      }
      continue;
    }

    for (dx = 0; dx < w; dx++) {
      const uint8_t* S = (uint8_t*)srcInfo.pixels + sy0 * srcInfo.strideInBytes + xofs[dx];
      int sum = 0;
      for (k = 0; k < area; k++) {
        sum += S[ofs[k]];
      }
      D[dx] = saturateCast<uint8_t>(static_cast<float>(sum) * scale);
    }

    for (; dx < dstWidth; dx++) {
      int sum = 0;
      int count = 0, sx0 = xofs[dx];
      if (sx0 >= srcWidth) {
        D[dx] = 0;
      }
      for (int sy = 0; sy < scaleY; sy++) {
        if (sy0 + sy >= srcInfo.h) {
          break;
        }
        const uint8_t* S = (uint8_t*)srcInfo.pixels + (sy0 + sy) * srcInfo.strideInBytes + sx0;
        for (int sx = 0; sx < scaleX * cn; sx += cn) {
          if (sx0 + sx >= srcWidth) {
            break;
          }
          sum += S[sx];
          count++;
        }
      }
      D[dx] = saturateCast<uint8_t>(static_cast<float>(sum) / static_cast<float>(count));
    }
  }
}

static void ResizeArea(const FastFuncInfo& srcInfo, FastFuncInfo& dstInfo,
                       const DecimateAlpha* xTab, int xTabSize, const DecimateAlpha* yTab, int,
                       const int* tabofs, int cn) {
  int dstWidth = dstInfo.w * cn;
  std::unique_ptr<float[]> _buffer(new float[static_cast<size_t>(dstWidth * 2)]);
  float* buf = _buffer.get();
  float* sum = buf + dstWidth;
  int jStart = tabofs[0], jEnd = tabofs[dstInfo.h], j, k, dx, prevDy = yTab[jStart].di;

  for (dx = 0; dx < dstWidth; dx++) {
    sum[dx] = 0.0f;
  }
  for (j = jStart; j < jEnd; j++) {
    float beta = yTab[j].alpha;
    int dy = yTab[j].di;
    int sy = yTab[j].si;
    {
      const uint8_t* S = (uint8_t*)srcInfo.pixels + sy * srcInfo.strideInBytes;
      for (dx = 0; dx < dstWidth; dx++) {
        buf[dx] = 0.0f;
      }
      if (cn == 1) {
        for (k = 0; k < xTabSize; k++) {
          int dxn = xTab[k].di;
          float alpha = xTab[k].alpha;
          buf[dxn] += static_cast<float>(S[xTab[k].si]) * alpha;
        }
      } else if (cn == 2) {
        for (k = 0; k < xTabSize; k++) {
          int sxn = xTab[k].si;
          int dxn = xTab[k].di;
          float alpha = xTab[k].alpha;
          float t0 = buf[dxn] + S[sxn] * alpha;
          float t1 = buf[dxn + 1] + S[sxn + 1] * alpha;
          buf[dxn] = t0;
          buf[dxn + 1] = t1;
        }
      } else if (cn == 3) {
        for (k = 0; k < xTabSize; k++) {
          int sxn = xTab[k].si;
          int dxn = xTab[k].di;
          float alpha = xTab[k].alpha;
          float t0 = buf[dxn] + S[sxn] * alpha;
          float t1 = buf[dxn + 1] + S[sxn + 1] * alpha;
          float t2 = buf[dxn + 2] + S[sxn + 2] * alpha;
          buf[dxn] = t0;
          buf[dxn + 1] = t1;
          buf[dxn + 2] = t2;
        }
      } else if (cn == 4) {
        for (k = 0; k < xTabSize; k++) {
          int sxn = xTab[k].si;
          int dxn = xTab[k].di;
          float alpha = xTab[k].alpha;
          float t0 = buf[dxn] + S[sxn] * alpha;
          float t1 = buf[dxn + 1] + S[sxn + 1] * alpha;
          buf[dxn] = t0;
          buf[dxn + 1] = t1;
          t0 = buf[dxn + 2] + S[sxn + 2] * alpha;
          t1 = buf[dxn + 3] + S[sxn + 3] * alpha;
          buf[dxn + 2] = t0;
          buf[dxn + 3] = t1;
        }
      } else {
        for (k = 0; k < xTabSize; k++) {
          int sxn = xTab[k].si;
          int dxn = xTab[k].di;
          float alpha = xTab[k].alpha;
          for (int c = 0; c < cn; c++) buf[dxn + c] += S[sxn + c] * alpha;
        }
      }
    }

    if (dy != prevDy) {
      saturateStore(sum, dstWidth, (uint8_t*)dstInfo.pixels + prevDy * dstInfo.strideInBytes);
      mul(buf, dstWidth, beta, sum);
      prevDy = dy;
    } else {
      mulAdd(buf, dstWidth, beta, sum);
    }
  }
  saturateStore(sum, dstWidth, (uint8_t*)dstInfo.pixels + prevDy * dstInfo.strideInBytes);
}

void boxFilterDownSampling(const void* inputPixels, int inputW, int inputH, int inputStrideInBytes,
                           void* outputPixels, int outputW, int outputH, int outputStrideInBytes,
                           PixelLayout pixelLayout) {
  ASSERT(inputPixels != nullptr && outputPixels != nullptr)
  ASSERT(inputW > 0 && inputH > 0 && outputW > 0 && outputH > 0)
  ASSERT(inputStrideInBytes >= 0 && outputStrideInBytes >= 0)
  ASSERT(outputW <= inputW && outputH <= inputH)

  auto scaleX = static_cast<double>(inputW) / outputW;
  auto scaleY = static_cast<double>(inputH) / outputH;
  int cn = static_cast<int>(pixelLayout);
  int iScaleX = saturateCast<int>(scaleX);
  int iScaleY = saturateCast<int>(scaleY);
  bool isAreaFast =
      std::abs(scaleX - iScaleX) < DBL_EPSILON && std::abs(scaleY - iScaleY) < DBL_EPSILON;

  FastFuncInfo srcInfo = {};
  srcInfo.pixels = const_cast<void*>(inputPixels);
  srcInfo.w = inputW;
  srcInfo.h = inputH;
  srcInfo.strideInBytes = inputStrideInBytes;

  FastFuncInfo dstInfo = {};
  dstInfo.pixels = outputPixels;
  dstInfo.w = outputW;
  dstInfo.h = outputH;
  dstInfo.strideInBytes = outputStrideInBytes;

  int k, sx, sy, dx, dy;

  if (isAreaFast) {
    int area = iScaleX * iScaleY;
    size_t srcStep = static_cast<size_t>(inputStrideInBytes / ChannelSizeInBytes);
    std::unique_ptr<int[]> data(new int[static_cast<size_t>(area + outputW * cn)]);
    int* ofs = data.get();
    int* xofs = ofs + area;

    for (sy = 0, k = 0; sy < iScaleY; sy++) {
      for (sx = 0; sx < iScaleX; sx++) {
        ofs[k++] = (int)((size_t)sy * srcStep + (size_t)(sx * cn));
      }
    }

    for (dx = 0; dx < outputW; dx++) {
      int j = dx * cn;
      sx = iScaleX * j;
      for (k = 0; k < cn; k++) {
        xofs[j + k] = sx + k;
      }
    }

    ResizeAreaFast(srcInfo, dstInfo, ofs, xofs, iScaleX, iScaleY, cn);
    return;
  }

  std::unique_ptr<DecimateAlpha[]> _xytab(
      new DecimateAlpha[static_cast<size_t>((inputW + inputH) * 2)]);
  DecimateAlpha* xtab = _xytab.get();
  DecimateAlpha* ytab = xtab + inputW * 2;
  int xtabSize = computeResizeAreaTab(inputW, outputW, cn, scaleX, xtab);
  int ytabSize = computeResizeAreaTab(inputH, outputH, 1, scaleY, ytab);
  std::unique_ptr<int[]> _tabofs(new int[static_cast<size_t>(outputH + 1)]);
  int* tabofs = _tabofs.get();
  for (k = 0, dy = 0; k < ytabSize; k++) {
    if (k == 0 || ytab[k].di != ytab[k - 1].di) {
      ASSERT(ytab[k].di == dy);
      tabofs[dy++] = k;
    }
  }
  tabofs[dy] = ytabSize;
  ResizeArea(srcInfo, dstInfo, xtab, xtabSize, ytab, ytabSize, tabofs, cn);
}
}  // namespace tgfx