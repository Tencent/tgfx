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

// Computes the signed distance from a point to a rounded rectangle centered at origin.
// Negative values are inside, positive values are outside.
static float RoundedRectSDF(float px, float py, float halfWidth, float halfHeight, float radius) {
  float qx = std::abs(px) - halfWidth + radius;
  float qy = std::abs(py) - halfHeight + radius;
  float outsideDist =
      std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
  float insideDist = std::min(std::max(qx, qy), 0.0f);
  return outsideDist + insideDist - radius;
}

// Computes the gradient (normal direction) of the rounded rect SDF at a point.
// Returns a normalized 2D vector pointing away from the nearest edge.
static void RoundedRectNormal(float px, float py, float halfWidth, float halfHeight, float radius,
                              float* nx, float* ny) {
  float qx = std::abs(px) - halfWidth + radius;
  float qy = std::abs(py) - halfHeight + radius;

  float gradX = 0.0f;
  float gradY = 0.0f;

  if (qx > 0.0f && qy > 0.0f) {
    // In the corner region: gradient points radially from corner center
    gradX = qx;
    gradY = qy;
  } else if (qx > qy) {
    // Closest to vertical edge
    gradX = 1.0f;
    gradY = 0.0f;
  } else {
    // Closest to horizontal edge
    gradX = 0.0f;
    gradY = 1.0f;
  }

  // Restore sign based on quadrant
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

// Derivative of the squircle surface function for computing surface slope.
// The surface function is f(t) = (1 - (1-t)^4)^0.25, mapping edge distance [0,1] to height.
static float SquircleSurfaceDerivative(float t) {
  if (t <= 0.0f || t >= 1.0f) {
    return 0.0f;
  }
  float oneMinusT = 1.0f - t;
  float p3 = oneMinusT * oneMinusT * oneMinusT;  // (1-t)^3
  float p4 = p3 * oneMinusT;                     // (1-t)^4
  float base = 1.0f - p4;
  if (base <= 0.0f) {
    return 0.0f;
  }
  // d/dt [ (1 - (1-t)^4)^0.25 ] = 0.25 * (1-(1-t)^4)^(-0.75) * 4*(1-t)^3
  //                                = (1-t)^3 / (1-(1-t)^4)^0.75
  return p3 / std::pow(base, 0.75f);
}

// Applies Snell's Law in 2D to compute the refraction displacement.
// incident: the viewing direction (assumed to be straight down, i.e. (0,0,-1) in 3D)
// surfaceNormal: 2D normal of the glass surface at this point
// ior: index of refraction of the glass
// Returns the 2D displacement (dx, dy) in pixels.
static void ComputeRefractionOffset(float surfaceSlope, float normalX, float normalY, float ior,
                                    float thickness, float* dx, float* dy) {
  // The surface slope determines the angle of incidence
  // For a ray going straight down hitting a tilted surface:
  // sin(theta_i) = surfaceSlope / sqrt(1 + surfaceSlope^2)
  float sinIncident = surfaceSlope / std::sqrt(1.0f + surfaceSlope * surfaceSlope);

  // Snell's Law: n1 * sin(theta_i) = n2 * sin(theta_t)
  // n1 = 1.0 (air), n2 = ior
  float sinRefracted = sinIncident / ior;
  sinRefracted = std::max(-1.0f, std::min(1.0f, sinRefracted));

  // The lateral displacement through a glass slab of given thickness:
  // displacement = thickness * tan(theta_t) - thickness * tan(theta_i)
  // Simplified: displacement = thickness * (tan(theta_t) - tan(theta_i))
  // But since we want the apparent shift of the background:
  // displacement = thickness * (sin(theta_i) - sin(theta_t)) / cos(theta_t)
  float cosRefracted = std::sqrt(1.0f - sinRefracted * sinRefracted);
  float displacement = 0.0f;
  if (cosRefracted > 0.001f) {
    displacement = thickness * (sinIncident - sinRefracted) / cosRefracted;
  }

  // Project the displacement along the 2D normal direction
  *dx = displacement * normalX;
  *dy = displacement * normalY;
}

std::shared_ptr<Image> GlassDisplacementMap::Generate(int width, int height, float cornerRadius,
                                                      float depth, float ior) {
  if (width <= 0 || height <= 0 || depth <= 0 || ior <= 1.0f) {
    return nullptr;
  }

  float halfWidth = static_cast<float>(width) * 0.5f;
  float halfHeight = static_cast<float>(height) * 0.5f;
  float radius = std::min(cornerRadius, std::min(halfWidth, halfHeight));
  float maxDepth = std::min(halfWidth, halfHeight) - 1.0f;
  depth = std::min(depth, maxDepth);

  // The glass "thickness" for Snell's law displacement calculation scales with depth
  float glassThickness = depth * 0.5f;

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      // Convert to centered coordinates
      float px = static_cast<float>(x) - halfWidth + 0.5f;
      float py = static_cast<float>(y) - halfHeight + 0.5f;

      // Compute SDF distance (negative = inside)
      float dist = RoundedRectSDF(px, py, halfWidth, halfHeight, radius);

      // Only pixels within the depth region get displacement
      // dist is negative inside, so edge distance from inside = -dist
      float edgeDist = -dist;

      float dispX = 0.0f;
      float dispY = 0.0f;

      if (edgeDist > 0.0f && edgeDist < depth) {
        // Normalized position within the refraction band [0=edge, 1=deep inside]
        float t = edgeDist / depth;

        // Compute surface slope from the squircle function
        float slope = SquircleSurfaceDerivative(t);

        // Get the 2D edge normal direction
        float nx = 0.0f;
        float ny = 0.0f;
        RoundedRectNormal(px, py, halfWidth, halfHeight, radius, &nx, &ny);

        // Compute refraction displacement
        ComputeRefractionOffset(slope, nx, ny, ior, glassThickness, &dispX, &dispY);
      }

      // Encode displacement to [0, 255] range. 128 = no displacement.
      // Normalize displacement relative to a maximum expected displacement
      float maxDisp = glassThickness * 2.0f;
      float encodedX = (dispX / maxDisp) * 0.5f + 0.5f;
      float encodedY = (dispY / maxDisp) * 0.5f + 0.5f;
      encodedX = std::max(0.0f, std::min(1.0f, encodedX));
      encodedY = std::max(0.0f, std::min(1.0f, encodedY));

      size_t offset =
          (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
      pixels[offset + 0] = static_cast<uint8_t>(encodedX * 255.0f);  // R = horizontal offset
      pixels[offset + 1] = static_cast<uint8_t>(encodedY * 255.0f);  // G = vertical offset
      pixels[offset + 2] = 0;                                        // B unused
      pixels[offset + 3] = 255;                                      // A opaque
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
