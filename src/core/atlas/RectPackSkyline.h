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
#include <cstdint>
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

  bool addRect(int w, int h, Point& location);

  float percentFull() const {
    return areaSoFar / static_cast<float>(_width * _height);
  }

 private:
  struct Node {
    int x;
    int y;
    int width;
  };

  bool rectangleFits(size_t skylineIndex, int w, int h, int& y) const;

  void addSkylineLevel(size_t skylineIndex, int x, int y, int width, int height);

  int _width;
  int _height;
  std::vector<Node> skyline;
  int32_t areaSoFar;
};

}  // namespace tgfx
