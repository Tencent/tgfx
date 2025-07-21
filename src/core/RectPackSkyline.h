/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <vector>
#include "tgfx/core/Point.h"

namespace tgfx {
class RectPackSkyline {
 public:
  RectPackSkyline(int width, int height) : _width(width), _height(height) {
    reset();
  }

  void reset() {
    areaSoFar = 0;
    skyline.clear();
    skyline.push_back({0, 0, _width});
  }

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  bool addRect(int width, int height, Point& location);

  float percentFull() const {
    return static_cast<float>(areaSoFar) / static_cast<float>(_width * _height);
  }

 private:
  struct Node {
    int x = 0;
    int y = 0;
    int width = 2;
  };

  bool rectangleFits(int skylineIndex, int width, int height, int& yPosition) const;

  void addSkylineLevel(int skylineIndex, int x, int y, int width, int height);

  std::vector<Node> skyline = {};
  int _width = 512;
  int _height = 512;
  int areaSoFar = 0;
};
}  // namespace tgfx
