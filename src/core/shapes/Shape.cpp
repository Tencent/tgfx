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

#include "tgfx/core/Shape.h"

namespace tgfx {
bool Shape::isLine(Point*) const {
  return false;
}

bool Shape::isRect(Rect*) const {
  return false;
}

bool Shape::isOval(Rect*) const {
  return false;
}

bool Shape::isRRect(RRect*) const {
  return false;
}

bool Shape::isSimplePath(Path*) const {
  return false;
}

bool Shape::isInverseFillType() const {
  return false;
}

Rect Shape::getBounds(float) const {
  return Rect::MakeEmpty();
}

Path Shape::getPath(float) const {
  return {};
}
}  // namespace tgfx