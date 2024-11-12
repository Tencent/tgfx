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

#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * Shape that contains a single path.
 */
class PathShape : public Shape {
 public:
  explicit PathShape(Path path) : path(std::move(path)) {
  }

  bool isLine(Point line[2] = nullptr) const override;

  bool isRect(Rect* rect = nullptr) const override;

  bool isOval(Rect* bounds = nullptr) const override;

  bool isRRect(RRect* rRect = nullptr) const override;

  bool isSimplePath(Path* path) const override;

  Rect getBounds(float resolutionScale = 1.0f) const override;

  Path getPath(float resolutionScale = 1.0f) const override;

 protected:
  Type type() const override {
    return Type::Path;
  }

  UniqueKey getUniqueKey() const override;

 private:
  Path path = {};
};
}  // namespace tgfx
