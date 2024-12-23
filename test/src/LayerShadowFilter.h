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

#include <tgfx/core/MaskFilter.h>
#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * Parameters for the drop shadow.
 */
struct LayerShadowParam {
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  float blurrinessX = 0.0f;
  float blurrinessY = 0.0f;
  Color color = Color::Black();

  bool operator==(const LayerShadowParam& value) const {
    return offsetX == value.offsetX && offsetY == value.offsetY &&
           blurrinessX == value.blurrinessX && blurrinessY == value.blurrinessY &&
           color == value.color;
  }
};

class LayerShadowFilter : public LayerFilter {
 public:
  /**
   * Create a filter that draws drop shadows under the input content.
   */
  static std::shared_ptr<LayerShadowFilter> Make(const std::vector<LayerShadowParam>& params);

  /**
   * The parameters for the drop shadows.
   */
  std::vector<LayerShadowParam> shadowParams() const {
    return params;
  }

  void setShadowParams(const std::vector<LayerShadowParam>& params);

  /**
   * Whether to show shadows behind transparent regions of the input content.
   */
  bool showBehindTransparent() const {
    return _showBehindTransparent;
  }

  void setShowBehindTransparent(bool showBehindTransparent);

  bool applyFilter(Canvas* canvas, std::shared_ptr<Image> image, float scale) override;

  Rect filterBounds(const Rect& srcRect, float scale) override;

 private:
  static std::shared_ptr<ImageFilter> CreateShadowFilter(const LayerShadowParam& param,
                                                         float scale);

  explicit LayerShadowFilter(const std::vector<LayerShadowParam>& params);

  void drawShadows(Canvas* canvas, std::shared_ptr<Image> image, float contentScale);

  std::vector<LayerShadowParam> params = {};

  bool _showBehindTransparent = false;
};

}  // namespace tgfx
