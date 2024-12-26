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
  if (pathProvider == nullptr) {
    return nullptr;
  }

  return std::make_shared<ExternalShape>(std::move(pathProvider));
}

bool ExternalShape::isLine(Point line[2]) const {
  return getPathInternal().isLine(line);
}

bool ExternalShape::isRect(Rect* rect) const {
  return getPathInternal().isRect(rect);
}

bool ExternalShape::isOval(Rect* bounds) const {
  return getPathInternal().isOval(bounds);
}

bool ExternalShape::isRRect(RRect* rRect) const {
  return getPathInternal().isRRect(rRect);
}

bool ExternalShape::isSimplePath(Path* resultPath) const {
  if (resultPath != nullptr) {
    *resultPath = getPathInternal();
  }
  return true;
}

bool ExternalShape::isInverseFillType() const {
  return getPathInternal().isInverseFillType();
}

Rect ExternalShape::getBounds(float resolutionScale) const {
  auto finalPath = getPathInternal();
  finalPath.transform(Matrix::MakeScale(resolutionScale));
  return finalPath.getBounds();
}

Path ExternalShape::getPath(float resolutionScale) const {
  auto finalPath = getPathInternal();
  finalPath.transform(Matrix::MakeScale(resolutionScale));
  return finalPath;
}

UniqueKey ExternalShape::getUniqueKey() const {
  return PathRef::GetUniqueKey(getPathInternal());
}

Path ExternalShape::getPathInternal() const {
  if (path.isEmpty()) {
    const auto tmpPath = const_cast<Path*>(&path);
    *tmpPath = provider->getPath();
  }
  return path;
}
}  // namespace tgfx
