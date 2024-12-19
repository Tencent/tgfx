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
/**
 * Shape that merges multiple shapes together.
 */
class MergeShape : public Shape {
 public:
  MergeShape(std::shared_ptr<Shape> first, std::shared_ptr<Shape> second, PathOp pathOp)
      : first(std::move(first)), second(std::move(second)), pathOp(pathOp) {
  }

  bool isRect(Rect* rect = nullptr) const override;

  bool isInverseFillType() const override;

  Rect getBounds(float resolutionScale = 1.0f) const override;

  Path getPath(float resolutionScale = 1.0f) const override;

 protected:
  Type type() const override {
    return Type::Merge;
  }

  UniqueKey getUniqueKey() const override {
    return uniqueKey.get();
  }

 private:
  LazyUniqueKey uniqueKey = {};
  std::shared_ptr<Shape> first = nullptr;
  std::shared_ptr<Shape> second = nullptr;
  PathOp pathOp = PathOp::Append;
};
}  // namespace tgfx
