/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
static constexpr int kDefaultPadding = 2;

class RectanglePack {
 public:
  explicit RectanglePack(int padding = kDefaultPadding) : padding(padding) {
    reset();
  }

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  Point addRect(int w, int h) {
    w += padding;
    h += padding;
    auto area = (_width - x) * (_height - y);
    if ((x + w - _width) * y > area || (y + h - _height) * x > area) {
      if (_width <= _height) {
        x = _width;
        y = padding;
        _width += w;
      } else {
        x = padding;
        y = _height;
        _height += h;
      }
    }
    auto point = Point::Make(x, y);
    if (x + w - _width < y + h - _height) {
      x += w;
      _width = std::max(_width, x);
      _height = std::max(_height, y + h);
    } else {
      y += h;
      _height = std::max(_height, y);
      _width = std::max(_width, x + w);
    }
    return point;
  }

  void reset() {
    _width = padding;
    _height = padding;
    x = padding;
    y = padding;
  }

 private:
  int padding = kDefaultPadding;
  int _width = 0;
  int _height = 0;
  int x = 0;
  int y = 0;
};
}  // namespace tgfx