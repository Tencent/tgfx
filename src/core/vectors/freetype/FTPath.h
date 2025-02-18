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

#include "FTUtil.h"
#include "tgfx/core/Path.h"
#include FT_STROKER_H

namespace tgfx {
struct FreetypeOutline {
  FT_Outline outline = {};
  std::vector<int16_t> contours = {};
};

class FTPath {
 public:
  bool isEvenOdd() const {
    return evenOdd;
  }

  void setEvenOdd(bool value) {
    evenOdd = value;
  }

  bool isEmpty() const;
  void moveTo(const Point& point);
  void lineTo(const Point& point);
  void quadTo(const Point& control, const Point& point);
  void cubicTo(const Point& control1, const Point& control2, const Point& point);
  void close();
  std::vector<std::shared_ptr<FreetypeOutline>> getOutlines() const;

 private:
  bool finalizeOutline(FreetypeOutline* outline, size_t startPointIndex) const;

  std::vector<FT_Vector> points = {};
  std::vector<PathVerb> verbs = {};
  std::vector<char> tags = {};
  std::vector<size_t> contours = {};
  bool evenOdd = false;
};
}  // namespace tgfx
