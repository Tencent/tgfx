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

#include "gpu/ResourceKey.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
class InverseShape : public Shape {
 public:
  explicit InverseShape(std::shared_ptr<Shape> shape) : shape(std::move(shape)) {
  }

  bool isLine(Point line[2] = nullptr) const override {
    return shape->isLine(line);
  }

  bool isRect(Rect* rect = nullptr) const override {
    return shape->isRect(rect);
  }

  bool isOval(Rect* bounds = nullptr) const override {
    return shape->isOval(bounds);
  }

  bool isRRect(RRect* rRect = nullptr) const override {
    return shape->isRRect(rRect);
  }

  bool isInverseFillType() const override {
    return !shape->isInverseFillType();
  }

  Rect getBounds() const override {
    return shape->getBounds();
  }

  Path getPath() const override;

 protected:
  Type type() const override {
    return Type::Inverse;
  }

  UniqueKey getUniqueKey() const override {
    return uniqueKey.get();
  }

 private:
  LazyUniqueKey uniqueKey = {};
  std::shared_ptr<Shape> shape = nullptr;

  friend class Shape;
};
}  // namespace tgfx
