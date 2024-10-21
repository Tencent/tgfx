/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <optional>
#include "tgfx/svg/node/SvgNode.h"

namespace tgfx {
class SvgRect : public SvgNode {
 public:
  static std::shared_ptr<SvgRect> Make() {
    return std::make_shared<SvgRect>();
  }

 private:
  SvgRect() : SvgNode(SvgTag::kRect) {
  }

 public:
  bool setAttribute(const std::string& attributeName, const std::string& attributeValue) override;

  float getX() const {
    return _x;
  }

  float getY() const {
    return _y;
  }

  float getWidth() const {
    return _width;
  }

  float getHeight() const {
    return _height;
  }

  std::optional<float> getRx() const {
    return _rx;
  }

  std::optional<float> getRy() const {
    return _ry;
  }

 private:
  float _x = 0.0f;
  float _y = 0.0f;
  float _width = 0.0f;
  float _height = 0.0f;
  std::optional<float> _rx;
  std::optional<float> _ry;
};
}  // namespace tgfx