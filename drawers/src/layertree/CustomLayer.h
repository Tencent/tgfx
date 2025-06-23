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

#include "tgfx/layers/Layer.h"

namespace drawers {
class CustomLayer : public tgfx::Layer {
 public:
  static std::shared_ptr<CustomLayer> Make();
  void setText(const std::string& text) {
    _text = text;
    invalidateContent();
  }

  void setFont(const tgfx::Font& font) {
    _font = font;
    invalidateContent();
  }

 protected:
  CustomLayer() = default;

  void onUpdateContent(tgfx::LayerRecorder* recorder) const override;

 private:
  std::string _text;
  tgfx::Font _font;
};

}  // namespace drawers
