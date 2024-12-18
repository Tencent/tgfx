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

struct LayerShadowParam {
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  float blurrinessX = 0.0f;
  float blurrinessY = 0.0f;
  Color color = Color::Black();
};

class LayerShadowFilter : public LayerFilter {
 public:
  /**
     * Create a filter that draws drop shadows under the input content.
     */
  static std::shared_ptr<LayerShadowFilter> Make(const std::vector<LayerShadowParam>& params);

  std::vector<LayerShadowParam> shadowParams() const {
    return params;
  }

  void setShadowParams(const std::vector<LayerShadowParam>& params);

  bool drawWithFilter(Canvas* canvas, std::shared_ptr<Picture> picture, float scale) override;

  Rect filterBounds(const Rect& srcRect, float scale) override;

 private:
  static std::shared_ptr<ImageFilter> createShadowFilter(const LayerShadowParam& param,
                                                         float scale);

  explicit LayerShadowFilter(const std::vector<LayerShadowParam>& params);

  std::vector<LayerShadowParam> params = {};
};

}  // namespace tgfx
