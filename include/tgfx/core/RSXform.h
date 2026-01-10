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

#pragma once

namespace tgfx {
/**
 * RSXform is a compressed form of a rotation+scale matrix.
 *
 * The transformation matrix is:
 *   | scos  -ssin   tx |
 *   | ssin   scos   ty |
 *   |   0      0     1 |
 *
 * Where scos = scale * cos(angle) and ssin = scale * sin(angle).
 */
struct RSXform {
  /**
   * Creates an RSXform with the specified values.
   * @param scos scale * cos(angle)
   * @param ssin scale * sin(angle)
   * @param tx x translation
   * @param ty y translation
   */
  static RSXform Make(float scos, float ssin, float tx, float ty) {
    return {scos, ssin, tx, ty};
  }

  /**
   * Creates an RSXform from rotation angle, scale, translation, and anchor point.
   * The anchor point (ax, ay) is in pixels of the source image, not normalized.
   * @param scale uniform scale factor
   * @param radians rotation angle in radians
   * @param tx x translation of the anchor point after transformation
   * @param ty y translation of the anchor point after transformation
   * @param ax x coordinate of the anchor point in source space
   * @param ay y coordinate of the anchor point in source space
   */
  static RSXform MakeFromRadians(float scale, float radians, float tx, float ty, float ax,
                                 float ay);

  float scos = 1.0f;  ///< scale * cos(angle)
  float ssin = 0.0f;  ///< scale * sin(angle)
  float tx = 0.0f;    ///< x translation
  float ty = 0.0f;    ///< y translation

  /**
   * Returns true if the transformation keeps rectangles axis-aligned (no rotation or 90-degree
   * rotation).
   */
  bool rectStaysRect() const {
    return scos == 0 || ssin == 0;
  }

  /**
   * Sets this RSXform to the identity transformation.
   */
  void setIdentity() {
    scos = 1.0f;
    ssin = 0.0f;
    tx = 0.0f;
    ty = 0.0f;
  }

  /**
   * Sets this RSXform to the specified values.
   */
  void set(float sCos, float sSin, float tX, float tY) {
    scos = sCos;
    ssin = sSin;
    tx = tX;
    ty = tY;
  }
};
}  // namespace tgfx
