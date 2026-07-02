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

#include "GlassDisplacementMap.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

static float RoundedRectSDF(float px, float py, float halfWidth, float halfHeight, float radius) {
  float qx = std::abs(px) - halfWidth + radius;
  float qy = std::abs(py) - halfHeight + radius;
  float outsideDist =
      std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
  float insideDist = std::min(std::max(qx, qy), 0.0f);
  return outsideDist + insideDist - radius;
}

// Squircle surface function: f(x) = pow(1 - pow(1-x, 4), 0.25)
static float SquircleSurface(float x) {
  x = std::max(0.0f, std::min(1.0f, x));
  float oneMinusX = 1.0f - x;
  float inner = oneMinusX * oneMinusX * oneMinusX * oneMinusX;
  return std::pow(1.0f - inner, 0.25f);
}

std::shared_ptr<Image> GlassDisplacementMap::Generate(int width, int height, float cornerRadius,
                                                      float depth, float ior, float* outMaxDisp) {
  if (width <= 0 || height <= 0 || depth <= 0 || ior <= 1.0f) {
    return nullptr;
  }

  float halfWidth = static_cast<float>(width) * 0.5f;
  float halfHeight = static_cast<float>(height) * 0.5f;
  float minHalf = std::min(halfWidth, halfHeight);
  float crRadius = std::min(cornerRadius, minHalf);

  float depthRatio = std::min(depth / (minHalf - 1.0f), 1.0f);
  // refraction 0~100 applies an additional thickness multiplier of 1.0~1.5.
  // ior = 1 + refraction/100 * 9, so refractionRatio = (ior - 1) / 9.
  float refractionRatio = (ior - 1.0f) / 9.0f;
  float thicknessMultiplier = 1.0f + refractionRatio * 0.5f;
  float glassThickness = (1.0f + depthRatio * (minHalf - 1.0f)) * thicknessMultiplier;

  float eta = 1.0f / ior;
  constexpr float eps = 0.5f;

  // Inner rounded rect (proportionally scaled) defines the flat region boundary.
  // depth 0~50 maps to flat region 100%~30%, depth 50~100 stays at 30%.
  float flatRatio = (depthRatio <= 0.5f) ? (1.0f - depthRatio * 1.4f) : 0.3f;
  float scale = flatRatio;
  float innerHalfW = halfWidth * scale;
  float innerHalfH = halfHeight * scale;
  float innerRadius = crRadius * scale;

  // Pass 1: compute all displacements and find actual maxDisp.
  size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
  std::vector<float> dispXArr(totalPixels, 0.0f);
  std::vector<float> dispYArr(totalPixels, 0.0f);
  float maxDisp = 0.0f;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float px = static_cast<float>(x) - halfWidth + 0.5f;
      float py = static_cast<float>(y) - halfHeight + 0.5f;

      // Outside the outer shape: no refraction.
      float outerSDF = RoundedRectSDF(px, py, halfWidth, halfHeight, crRadius);
      if (outerSDF >= 0.0f) {
        continue;
      }
      // Inside the inner (scaled) shape: flat region, no refraction.
      float innerSDF = RoundedRectSDF(px, py, innerHalfW, innerHalfH, innerRadius);
      if (innerSDF < 0.0f) {
        continue;
      }

      // In the edge band between outer and inner shapes.
      // xNorm: 0 at outer edge, 1 at inner edge.
      float edgeDist = -outerSDF;
      float totalDist = edgeDist + innerSDF;
      float xNorm = (totalDist > 0.001f) ? edgeDist / totalDist : 0.0f;
      xNorm = std::min(xNorm, 1.0f);
      float h = glassThickness;

      // Normal direction: radial from pixel toward center (inward tilt).
      float dirLen = std::sqrt(px * px + py * py);
      float gradX, gradY;
      if (dirLen > 0.001f) {
        gradX = -px / dirLen;
        gradY = -py / dirLen;
      } else {
        continue;
      }

      // Surface slope: use xNorm derivative w.r.t. edgeDist.
      float xNormP = std::min((edgeDist + eps) / totalDist, 1.0f);
      float xNormM = std::max((edgeDist - eps) / totalDist, 0.0f);
      float hP = SquircleSurface(xNormP) * glassThickness;
      float hM = SquircleSurface(xNormM) * glassThickness;
      float dhdEdge = (hP - hM) / (2.0f * eps);

      // Surface normal.
      float nx = gradX * dhdEdge;
      float ny = gradY * dhdEdge;
      float nz = 1.0f;
      float normalLen = std::sqrt(nx * nx + ny * ny + nz * nz);
      nx /= normalLen;
      ny /= normalLen;
      nz /= normalLen;

      // Snell's Law.
      float cosI = nz;
      float sinR2 = eta * eta * (1.0f - cosI * cosI);
      if (sinR2 < 1.0f) {
        float cosR = std::sqrt(1.0f - sinR2);
        float factor = eta * cosI - cosR;
        float tx = factor * nx;
        float ty = factor * ny;
        float tz = eta * (-1.0f) + factor * nz;
        if (std::abs(tz) > 0.001f) {
          float dispX = (tx / tz) * h;
          float dispY = (ty / tz) * h;
          size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
          dispXArr[idx] = dispX;
          dispYArr[idx] = dispY;
          maxDisp = std::max(maxDisp, std::max(std::abs(dispX), std::abs(dispY)));
        }
      }
    }
  }

  if (maxDisp < 1.0f) {
    maxDisp = 1.0f;
  }

  // Use the actual max displacement as the encoding reference so the displacement map
  // covers the full [0,1] range regardless of refraction value.
  float fixedMax = maxDisp;

  if (outMaxDisp) {
    *outMaxDisp = fixedMax;
  }

  // Pass 2: encode to pixels.
  std::vector<uint8_t> pixels(totalPixels * 4);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
      float encodedX = (dispXArr[idx] / fixedMax) * 0.5f + 0.5f;
      float encodedY = (dispYArr[idx] / fixedMax) * 0.5f + 0.5f;
      encodedX = std::max(0.0f, std::min(1.0f, encodedX));
      encodedY = std::max(0.0f, std::min(1.0f, encodedY));

      size_t offset = idx * 4;
      pixels[offset + 0] = static_cast<uint8_t>(encodedX * 255.0f);
      pixels[offset + 1] = static_cast<uint8_t>(encodedY * 255.0f);
      pixels[offset + 2] = 0;
      pixels[offset + 3] = 255;
    }
  }

  Bitmap bitmap(width, height, false, false);
  if (bitmap.isEmpty()) {
    return nullptr;
  }
  auto srcInfo = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Opaque,
                                 static_cast<size_t>(width) * 4);
  bitmap.writePixels(srcInfo, pixels.data());
  return Image::MakeFrom(bitmap);
}

}  // namespace tgfx
