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

static constexpr float PI = 3.14159265358979323846f;

static float RoundedRectSDF(float px, float py, float halfWidth, float halfHeight, float radius) {
  float qx = std::abs(px) - halfWidth + radius;
  float qy = std::abs(py) - halfHeight + radius;
  float outsideDist = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                                std::max(qy, 0.0f) * std::max(qy, 0.0f));
  float insideDist = std::min(std::max(qx, qy), 0.0f);
  return outsideDist + insideDist - radius;
}

// SDF-based smooth surface height: cosine profile from edge (h=0) to interior (h=sagitta).
static float SDFHeight(float px, float py, float halfWidth, float halfHeight, float radius,
                       float depth, float sagitta) {
  float dist = RoundedRectSDF(px, py, halfWidth, halfHeight, radius);
  float edgeDist = -dist;
  if (edgeDist <= 0.0f) {
    return 0.0f;
  }
  if (edgeDist >= depth) {
    return sagitta;
  }
  float t = edgeDist / depth;
  return sagitta * std::sin(t * PI * 0.5f);
}

std::shared_ptr<Image> GlassDisplacementMap::Generate(int width, int height, float cornerRadius,
                                                      float depth, float ior) {
  if (width <= 0 || height <= 0 || depth <= 0 || ior <= 1.0f) {
    return nullptr;
  }

  float halfWidth = static_cast<float>(width) * 0.5f;
  float halfHeight = static_cast<float>(height) * 0.5f;
  float crRadius = std::min(cornerRadius, std::min(halfWidth, halfHeight));
  float maxDepth = std::min(halfWidth, halfHeight) - 1.0f;
  depth = std::min(depth, maxDepth);

  float sagitta = depth * 0.5f;

  // Sphere for global lens normal: radius = 2 * max(halfW, halfH),
  // centered at (0, 0, -sphereR) so the glass surface is inscribed at z=0.
  float sphereR = 2.0f * std::max(halfWidth, halfHeight);

  // Precompute max displacement for encoding normalization.
  float maxSlope = sagitta * PI * 0.5f / depth;
  float nLen = std::sqrt(maxSlope * maxSlope + 1.0f);
  float maxSinI = maxSlope / nLen;
  float maxSinR = std::min(maxSinI / ior, 0.99f);
  float maxCosI = 1.0f / nLen;
  float maxCosR = std::sqrt(1.0f - maxSinR * maxSinR);
  float maxTanI = maxSinI / std::max(maxCosI, 0.001f);
  float maxTanR = maxSinR / std::max(maxCosR, 0.001f);
  float maxDisp = sagitta * std::abs(maxTanI - maxTanR) * 1.5f;
  if (maxDisp < 1.0f) {
    maxDisp = 1.0f;
  }

  float eta = 1.0f / ior;
  constexpr float eps = 0.5f;

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float px = static_cast<float>(x) - halfWidth + 0.5f;
      float py = static_cast<float>(y) - halfHeight + 0.5f;

      float dispX = 0.0f;
      float dispY = 0.0f;

      float h = SDFHeight(px, py, halfWidth, halfHeight, crRadius, depth, sagitta);

      if (h > 0.001f) {
        // --- Normal 1: SDF-based surface normal (numerical derivatives) ---
        float hxp = SDFHeight(px + eps, py, halfWidth, halfHeight, crRadius, depth, sagitta);
        float hxm = SDFHeight(px - eps, py, halfWidth, halfHeight, crRadius, depth, sagitta);
        float hyp = SDFHeight(px, py + eps, halfWidth, halfHeight, crRadius, depth, sagitta);
        float hym = SDFHeight(px, py - eps, halfWidth, halfHeight, crRadius, depth, sagitta);
        float dhdx = (hxp - hxm) / (2.0f * eps);
        float dhdy = (hyp - hym) / (2.0f * eps);

        float sdfNx = -dhdx;
        float sdfNy = -dhdy;
        float sdfNz = 1.0f;
        float sdfLen = std::sqrt(sdfNx * sdfNx + sdfNy * sdfNy + sdfNz * sdfNz);
        sdfNx /= sdfLen;
        sdfNy /= sdfLen;
        sdfNz /= sdfLen;

        // --- Normal 2: Global sphere normal ---
        // Sphere center at (0, 0, -sphereR), pixel at (px, py, h).
        // Vector from sphere center to pixel = (px, py, h + sphereR)
        float gx = px;
        float gy = py;
        float gz = h + sphereR;
        float gLen = std::sqrt(gx * gx + gy * gy + gz * gz);
        float sphereNx = gx / gLen;
        float sphereNy = gy / gLen;
        float sphereNz = gz / gLen;

        // --- Blend: SDF normal only within cornerRadius distance from edge ---
        float dist = RoundedRectSDF(px, py, halfWidth, halfHeight, crRadius);
        float edgeDist = -dist;
        float sdfWeight = 0.0f;
        if (edgeDist < crRadius && crRadius > 0.0f) {
          float t = edgeDist / crRadius;
          sdfWeight = (1.0f - t) * (1.0f - t);
        }

        float nx = sdfNx * sdfWeight + sphereNx * (1.0f - sdfWeight);
        float ny = sdfNy * sdfWeight + sphereNy * (1.0f - sdfWeight);
        float nz = sdfNz * sdfWeight + sphereNz * (1.0f - sdfWeight);
        float finalLen = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= finalLen;
        ny /= finalLen;
        nz /= finalLen;

        // --- 3D Snell's Law ---
        float cosI = nz;
        float sinR2 = eta * eta * (1.0f - cosI * cosI);
        if (sinR2 < 1.0f) {
          float cosR = std::sqrt(1.0f - sinR2);
          float factor = eta * cosI - cosR;
          float tx = factor * nx;
          float ty = factor * ny;
          float tz = eta * (-1.0f) + factor * nz;
          if (std::abs(tz) > 0.001f) {
            dispX = (tx / tz) * h;
            dispY = (ty / tz) * h;
          }
        }
      }

      float encodedX = (dispX / maxDisp) * 0.5f + 0.5f;
      float encodedY = (dispY / maxDisp) * 0.5f + 0.5f;
      encodedX = std::max(0.0f, std::min(1.0f, encodedX));
      encodedY = std::max(0.0f, std::min(1.0f, encodedY));

      size_t offset =
          (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
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
