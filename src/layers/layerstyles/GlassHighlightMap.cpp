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

#include "GlassHighlightMap.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

static constexpr float PI = 3.14159265358979323846f;

// Computes the signed distance from a point to a rounded rectangle centered at origin.
static float HighlightRoundedRectSDF(float px, float py, float halfWidth, float halfHeight,
                                     float radius) {
  float qx = std::abs(px) - halfWidth + radius;
  float qy = std::abs(py) - halfHeight + radius;
  float outsideDist =
      std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
  float insideDist = std::min(std::max(qx, qy), 0.0f);
  return outsideDist + insideDist - radius;
}

// Computes the 2D normal direction of the rounded rect SDF gradient at a point.
static void HighlightRoundedRectNormal(float px, float py, float halfWidth, float halfHeight,
                                       float radius, float* nx, float* ny) {
  float qx = std::abs(px) - halfWidth + radius;
  float qy = std::abs(py) - halfHeight + radius;

  float gradX = 0.0f;
  float gradY = 0.0f;

  if (qx > 0.0f && qy > 0.0f) {
    gradX = qx;
    gradY = qy;
  } else if (qx > qy) {
    gradX = 1.0f;
    gradY = 0.0f;
  } else {
    gradX = 0.0f;
    gradY = 1.0f;
  }

  if (px < 0.0f) {
    gradX = -gradX;
  }
  if (py < 0.0f) {
    gradY = -gradY;
  }

  float len = std::sqrt(gradX * gradX + gradY * gradY);
  if (len > 0.0f) {
    *nx = gradX / len;
    *ny = gradY / len;
  } else {
    *nx = 0.0f;
    *ny = 0.0f;
  }
}

// Attempt to smoothstep for edge transition.
static float Smoothstep(float edge0, float edge1, float x) {
  if (edge1 <= edge0) {
    return x >= edge0 ? 1.0f : 0.0f;
  }
  float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
  return t * t * (3.0f - 2.0f * t);
}

GlassHighlightMap::Result GlassHighlightMap::Generate(int width, int height, float cornerRadius,
                                                      float depth, float splay, float lightAngle,
                                                      float intensity) {
  Result result = {};
  if (width <= 0 || height <= 0 || depth <= 0 || intensity <= 0.0f) {
    return result;
  }

  float halfWidth = static_cast<float>(width) * 0.5f;
  float halfHeight = static_cast<float>(height) * 0.5f;
  float radius = std::min(cornerRadius, std::min(halfWidth, halfHeight));
  float maxDepth = std::min(halfWidth, halfHeight) - 1.0f;
  depth = std::min(depth, maxDepth);

  // Splay parameter mapping
  float specularExponent = 8.0f - (splay / 100.0f) * 7.0f;  // [8.0 → 1.0]
  float highlightWidth = 0.2f + (splay / 100.0f) * 0.8f;    // [0.2 → 1.0]

  // Light direction from angle
  float angleRad = lightAngle * PI / 180.0f;
  float lightDirX = std::cos(angleRad);
  float lightDirY = std::sin(angleRad);

  auto pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
  std::vector<uint8_t> highlightPixels(pixelCount * 4);
  std::vector<uint8_t> shadowPixels(pixelCount * 4);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float px = static_cast<float>(x) - halfWidth + 0.5f;
      float py = static_cast<float>(y) - halfHeight + 0.5f;

      float dist = HighlightRoundedRectSDF(px, py, halfWidth, halfHeight, radius);
      float edgeDist = -dist;  // positive inside

      size_t offset =
          (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;

      uint8_t highlightAlpha = 0;
      uint8_t shadowAlpha = 0;
      uint8_t shadowIsLight = 0;  // 0 = shadow (dark), 255 = highlight (light)

      if (edgeDist > 0.0f && edgeDist < depth) {
        float normalizedEdge = edgeDist / depth;  // [0=edge, 1=deep inside]

        float nx = 0.0f;
        float ny = 0.0f;
        HighlightRoundedRectNormal(px, py, halfWidth, halfHeight, radius, &nx, &ny);

        // Specular highlight: |dot(normal, lightDir)| with edge fade
        float absDot = std::abs(nx * lightDirX + ny * lightDirY);

        // Edge fade using splay-controlled width with smoothstep
        float splayFade = Smoothstep(1.0f - highlightWidth, 1.0f, 1.0f - normalizedEdge);
        float specular = std::pow(absDot * splayFade, specularExponent) * intensity;
        specular = std::min(1.0f, specular);
        highlightAlpha = static_cast<uint8_t>(specular * 255.0f);

        // Inner shadow/highlight: directional (no abs)
        float dirDot = nx * lightDirX + ny * lightDirY;
        // fade factor: stronger near edge, fades toward interior
        float fade = 1.0f - normalizedEdge;
        if (dirDot < 0.0f) {
          // Facing light → inner shadow (dark)
          float shadowStrength = std::min(1.0f, -dirDot * fade * fade * 0.4f * intensity);
          shadowAlpha = static_cast<uint8_t>(shadowStrength * 255.0f);
          shadowIsLight = 0;
        } else {
          // Away from light → inner highlight (light)
          float lightStrength = std::min(1.0f, dirDot * fade * fade * 0.6f * intensity);
          shadowAlpha = static_cast<uint8_t>(lightStrength * 255.0f);
          shadowIsLight = 255;
        }
      }

      // Highlight map: white with varying alpha (premultiplied)
      highlightPixels[offset + 0] = highlightAlpha;  // R
      highlightPixels[offset + 1] = highlightAlpha;  // G
      highlightPixels[offset + 2] = highlightAlpha;  // B
      highlightPixels[offset + 3] = highlightAlpha;  // A

      // Inner shadow map: black (shadow) or white (highlight) with varying alpha (premultiplied)
      shadowPixels[offset + 0] = shadowIsLight == 255 ? shadowAlpha : static_cast<uint8_t>(0);
      shadowPixels[offset + 1] = shadowIsLight == 255 ? shadowAlpha : static_cast<uint8_t>(0);
      shadowPixels[offset + 2] = shadowIsLight == 255 ? shadowAlpha : static_cast<uint8_t>(0);
      shadowPixels[offset + 3] = shadowAlpha;
    }
  }

  // Create highlight image
  Bitmap highlightBitmap(width, height, false, false);
  if (!highlightBitmap.isEmpty()) {
    auto srcInfo = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied,
                                   static_cast<size_t>(width) * 4);
    highlightBitmap.writePixels(srcInfo, highlightPixels.data());
    result.highlight = Image::MakeFrom(highlightBitmap);
  }

  // Create inner shadow image
  Bitmap shadowBitmap(width, height, false, false);
  if (!shadowBitmap.isEmpty()) {
    auto srcInfo = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied,
                                   static_cast<size_t>(width) * 4);
    shadowBitmap.writePixels(srcInfo, shadowPixels.data());
    result.innerShadow = Image::MakeFrom(shadowBitmap);
  }

  return result;
}

}  // namespace tgfx
