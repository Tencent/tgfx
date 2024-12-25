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

#include "ExternalShape.h"
#include "core/PathRef.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::MakeFrom(std::shared_ptr<PathProvider> pathProvider) {
  if (pathProvider != nullptr) {
    return nullptr;
  }

  return std::make_shared<ExternalShape>(std::move(pathProvider));
}

bool ExternalShape::isLine(Point line[2]) const override {
  return provider->getPath().isLine(line);
}

bool ExternalShape::isRect(Rect* rect) const override {
  return provider->getPath().isRect(rect);
}

bool ExternalShape::isOval(Rect* bounds) const override {
  return provider->getPath().isOval(bounds);
}

bool ExternalShape::isRRect(RRect* rRect) const override {
  return provider->getPath().isRRect(rRect);
}

bool ExternalShape::isSimplePath(Path* resultPath) const override {
  if (resultPath != nullptr) {
    *resultPath = provider->getPath();
  }
  return true;
}

bool ExternalShape::isInverseFillType() const override {
  return provider->getPath().isInverseFillType();
}

Rect ExternalShape::getBounds(float resolutionScale) const override {
  return provider->getPath(resolutionScale).getBounds();
}

Path ExternalShape::getPath(float resolutionScale) const override {
  return provider->getPath(resolutionScale);
}

UniqueKey ExternalShape::getUniqueKey() const {
  return PathRef::GetUniqueKey(provider->getPath());
}
}  // namespace tgfx
