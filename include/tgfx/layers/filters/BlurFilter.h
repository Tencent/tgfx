/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

class BlurFilter : public LayerFilter {
 public:
  virtual ~BlurFilter() = default;

  /**
   * Create a filter that blurs its input by the separate X and Y blurriness. The provided tile mode
   * is used when the blur kernel goes outside the input image.
   */
  static std::shared_ptr<BlurFilter> Make(float blurrinessX, float blurrinessY,
                                          TileMode tileMode = TileMode::Decal);

  /**
   * The Gaussian sigma value for blurring along the Y axis.
   */
  float blurrinessX() const {
    return _blurrinessX;
  }

  /**
   * Set the Gaussian sigma value for blurring along the X axis.
   */
  void setBlurrinessX(float blurrinessX);

  /**
   * The Gaussian sigma value for blurring along the Y axis.
   */
  float blurrinessY() const {
    return _blurrinessY;
  }

  /**
   * Set the Gaussian sigma value for blurring along the Y axis.
   * @param blurrinessY
   */
  void setBlurrinessY(float blurrinessY);

  /**
   * The tile mode applied at edges.
   */
  TileMode tileMode() const {
    return _tileMode;
  }

  /**
   * Set the tile mode applied at edges.
   * @param tileMode
   */
  void setTileMode(TileMode tileMode);

 protected:
  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

 private:
  BlurFilter(float blurrinessX, float blurrinessY, TileMode tileMode);
  float _blurrinessX = 0.0f;
  float _blurrinessY = 0.0f;
  TileMode _tileMode = TileMode::Decal;
};

}  // namespace tgfx
