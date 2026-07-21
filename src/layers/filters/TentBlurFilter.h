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

#include "tgfx/core/TileMode.h"
#include "tgfx/layers/filters/LayerImageFilter.h"

namespace tgfx {

/**
 * A filter that blurs its input using a triangular (tent) kernel. The triangular kernel is the
 * convolution of two box kernels, producing a piecewise-quadratic response that approximates a
 * linear unsigned distance field (UDF) when applied to binary alpha masks. This makes it preferable
 * over Gaussian blur for generating height maps used in glass refraction effects.
 */
class TentBlurFilter : public LayerImageFilter {
 public:
  /**
   * Creates a tent blur filter with the specified blur radius. The radius is applied symmetrically
   * along both axes.
   * @param blurrinessX The blur radius along the X axis in pixels.
   * @param blurrinessY The blur radius along the Y axis in pixels.
   * @param tileMode The tile mode applied when the blur kernel samples outside the input image.
   */
  static std::shared_ptr<TentBlurFilter> Make(float blurrinessX, float blurrinessY,
                                              TileMode tileMode = TileMode::Decal);

  /**
   * Creates a composed ImageFilter that applies a tent blur with the specified radius.
   * This is a convenience method for callers that need an ImageFilter directly (e.g., for
   * makeWithFilter) without going through the LayerImageFilter pipeline.
   * @param radius The blur radius in pixels (applied symmetrically on both axes).
   * @return A composed ImageFilter (horizontal + vertical passes), or nullptr if radius < 1.
   */
  static std::shared_ptr<ImageFilter> MakeImageFilter(float radius,
                                                      TileMode tileMode = TileMode::Clamp);

  /**
   * The blur radius along the X axis.
   */
  float blurrinessX() const {
    return _blurrinessX;
  }

  /**
   * Sets the blur radius along the X axis.
   */
  void setBlurrinessX(float blurrinessX);

  /**
   * The blur radius along the Y axis.
   */
  float blurrinessY() const {
    return _blurrinessY;
  }

  /**
   * Sets the blur radius along the Y axis.
   */
  void setBlurrinessY(float blurrinessY);

  /**
   * The tile mode applied at edges.
   */
  TileMode tileMode() const {
    return _tileMode;
  }

  /**
   * Sets the tile mode applied at edges.
   */
  void setTileMode(TileMode tileMode);

 protected:
  Type type() const override {
    return Type::BlurFilter;
  }

  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

 private:
  TentBlurFilter(float blurrinessX, float blurrinessY, TileMode tileMode);

  float _blurrinessX = 0.0f;
  float _blurrinessY = 0.0f;
  TileMode _tileMode = TileMode::Decal;
};

}  // namespace tgfx
