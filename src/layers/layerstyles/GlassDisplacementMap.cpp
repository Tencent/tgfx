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
  float outsideDist = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                                std::max(qy, 0.0f) * std::max(qy, 0.0f));
  float insideDist = std::min(std::max(qx, qy), 0.0f);
  return outsideDist + insideDist - radius;
}

// Distance from center (0,0) to the rounded-rectangle boundary along the direction of (px, py).
// Uses binary search on the ray from origin through (px, py) to find the zero-crossing of the SDF.
static float DistToBoundary(float px, float py, float halfWidth, float halfHeight, float radius) {
  float len = std::sqrt(px * px + py * py);
  if (len < 0.001f) {
    return std::min(halfWidth, halfHeight);
  }
  float dx = px / len;
  float dy = py / len;
  // Upper bound: the point is inside, so the boundary is at least at distance len.
  // Maximum possible distance from center to boundary is sqrt(halfW^2 + halfH^2).
  float lo = 0.0f;
  float hi = std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight);
  for (int i = 0; i < 16; i++) {
    float mid = (lo + hi) * 0.5f;
    float sdf = RoundedRectSDF(dx * mid, dy * mid, halfWidth, halfHeight, radius);
    if (sdf < 0.0f) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return (lo + hi) * 0.5f;
}

// Elliptical arc height profile for a convex glass surface.
// t = |OP| / |OB| where O is center, P is current pixel, B is the boundary along OP direction.
// t=0 at center, t=1 at boundary.
// h(t) = edgeThickness + (sagitta - edgeThickness) * sqrt(1 - t^2)
// Center (t=0): h = sagitta. Edge (t=1): h = edgeThickness, slope -> infinity.
static float SDFHeight(float px, float py, float halfWidth, float halfHeight, float radius,
                       float sagitta, float edgeThickness) {
  float dist = RoundedRectSDF(px, py, halfWidth, halfHeight, radius);
  if (dist >= 0.0f) {
    return 0.0f;
  }
  float distToCenter = std::sqrt(px * px + py * py);
  float distToBnd = DistToBoundary(px, py, halfWidth, halfHeight, radius);
  float t = (distToBnd > 0.001f) ? std::min(distToCenter / distToBnd, 1.0f) : 0.0f;
  return edgeThickness + (sagitta - edgeThickness) * std::sqrt(1.0f - t * t);
}

std::shared_ptr<Image> GlassDisplacementMap::Generate(int width, int height, float cornerRadius,
                                                      float depth, float ior) {
  if (width <= 0 || height <= 0 || depth <= 0 || ior <= 1.0f) {
    return nullptr;
  }

  float halfWidth = static_cast<float>(width) * 0.5f;
  float halfHeight = static_cast<float>(height) * 0.5f;
  float crRadius = std::min(cornerRadius, std::min(halfWidth, halfHeight));

  // depth controls overall glass thickness (sagitta).
  float maxThickness = std::min(halfWidth, halfHeight);
  float sagitta = std::min(depth, maxThickness);

  // edgeThickness: minimum glass thickness at the edge.
  float edgeThickness = std::max(sagitta * 0.3f, 10.0f);
  edgeThickness = std::min(edgeThickness, sagitta);

  // maxInterior: maximum SDF edgeDist at the center of the shape.
  float maxInterior = std::min(halfWidth, halfHeight);

  // Precompute max displacement for encoding normalization.
  // Elliptical arc: dh/d(edgeDist) = heightRange/maxInterior * (1-t)/sqrt(1-(1-t)^2).
  // At edgeDist=1, t_min=1/maxInterior, slope is large.
  float heightRange = sagitta - edgeThickness;
  float tMin = std::min(1.0f / maxInterior, 0.99f);
  float oneMinusTMin = 1.0f - tMin;
  float denom = std::sqrt(1.0f - oneMinusTMin * oneMinusTMin);
  float maxSlope =
      (denom > 0.001f) ? (heightRange / maxInterior) * oneMinusTMin / denom : 100.0f;
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

  fprintf(stderr,
          "[DispMap] w=%d h=%d sagitta=%.1f edgeH=%.1f maxInt=%.1f maxSlope=%.3f "
          "maxDisp=%.3f ior=%.2f crRadius=%.1f\n",
          width, height, sagitta, edgeThickness, maxInterior, maxSlope, maxDisp, ior, crRadius);

  float eta = 1.0f / ior;
  constexpr float eps = 0.5f;

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float px = static_cast<float>(x) - halfWidth + 0.5f;
      float py = static_cast<float>(y) - halfHeight + 0.5f;

      float dispX = 0.0f;
      float dispY = 0.0f;

      float h = SDFHeight(px, py, halfWidth, halfHeight, crRadius, sagitta, edgeThickness);

      if (h > 0.001f) {
        float hxp = SDFHeight(px + eps, py, halfWidth, halfHeight, crRadius, sagitta,
                              edgeThickness);
        float hxm = SDFHeight(px - eps, py, halfWidth, halfHeight, crRadius, sagitta,
                              edgeThickness);
        float hyp = SDFHeight(px, py + eps, halfWidth, halfHeight, crRadius, sagitta,
                              edgeThickness);
        float hym = SDFHeight(px, py - eps, halfWidth, halfHeight, crRadius, sagitta,
                              edgeThickness);
        float dhdx = (hxp - hxm) / (2.0f * eps);
        float dhdy = (hyp - hym) / (2.0f * eps);

        float nx = -dhdx;
        float ny = -dhdy;
        float nz = 1.0f;
        float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= nLen;
        ny /= nLen;
        nz /= nLen;

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
            dispX = (tx / tz) * sagitta;
            dispY = (ty / tz) * sagitta;
          }
        }
      }

      if ((x == width / 2 && y == height / 2) || (x == 1 && y == height / 2) ||
          (x == width / 4 && y == height / 2)) {
        fprintf(stderr, "[px(%d,%d)] h=%.4f dispX=%.4f dispY=%.4f dispX/maxDisp=%.6f\n", x, y, h,
                dispX, dispY, dispX / maxDisp);
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
