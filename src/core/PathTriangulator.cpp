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

#include "PathTriangulator.h"
#include "PathRef.h"
#include "pathkit.h"

namespace tgfx {
int PathTriangulator::GetAATriangleCount(size_t bufferSize) {
  return static_cast<int>(bufferSize / (sizeof(float) * 3));
}

PathTriangulator::PathTriangulator(Path path, const Rect& clipBounds, float tolerance)
    : path(std::move(path)), clipBounds(clipBounds), tolerance(tolerance) {
}

int PathTriangulator::toTriangles(std::vector<float>* vertices) const {
  const auto& skPath = PathRef::ReadAccess(path);
  return skPath.toAATriangles(tolerance, *reinterpret_cast<const pk::SkRect*>(&clipBounds),
                              vertices);
}

std::shared_ptr<Data> PathTriangulator::getData() const {
  std::vector<float> vertices = {};
  int count = toTriangles(&vertices);
  if (count == 0) {
    // The path is not a filled path, or it is invisible.
    return nullptr;
  }
  return Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(float));
}

}  // namespace tgfx
