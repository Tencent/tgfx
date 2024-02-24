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

#include "PathAATriangles.h"
#include "core/PathTriangulator.h"
#include "tgfx/core/PathEffect.h"

namespace tgfx {
std::shared_ptr<PathAATriangles> PathAATriangles::Make(Path path, const Matrix& matrix,
                                                       const Stroke* stroke) {
  if (path.isEmpty()) {
    return nullptr;
  }
  return std::shared_ptr<PathAATriangles>(new PathAATriangles(std::move(path), matrix, stroke));
}

PathAATriangles::PathAATriangles(Path path, const Matrix& matrix, const Stroke* s)
    : path(std::move(path)), matrix(matrix) {
  if (s) {
    stroke = new Stroke(*s);
  }
}

PathAATriangles::~PathAATriangles() {
  delete stroke;
}

std::shared_ptr<Data> PathAATriangles::getData() const {
  std::vector<float> vertices = {};
  auto finalPath = path;
  auto effect = PathEffect::MakeStroke(stroke);
  if (effect != nullptr) {
    effect->applyTo(&finalPath);
  }
  finalPath.transform(matrix);
  auto clipBounds = finalPath.getBounds();
  auto count = PathTriangulator::ToAATriangles(finalPath, clipBounds, &vertices);
  if (count == 0) {
    // The path is not a filled path, or it is invisible.
    return nullptr;
  }
  return Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(float));
}
}  // namespace tgfx
