/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include "gpu/ir/CoordinateBinder.h"

namespace tgfx {
namespace {
constexpr char TRANSFORM_UNIFORM_PREFIX[] = "CoordTransformMatrix_";
constexpr char TRANSFORM_VARYING_PREFIX[] = "TransformedCoords_";
}  // namespace

Matrix CoordinateBinder::CombinedMatrix(const CoordTransform& transform, const Matrix& uvMatrix) {
  Matrix combined = {};
  combined.setConcat(transform.getTotalMatrix(), uvMatrix);
  return combined;
}

bool CoordinateBinder::HasPerspective(bool geometryHasPerspective,
                                      const CoordTransform& transform) {
  return geometryHasPerspective || transform.matrix.hasPerspective();
}

SLType CoordinateBinder::VaryingType(bool hasPerspective) {
  return hasPerspective ? SLType::Float3 : SLType::Float2;
}

std::string CoordinateBinder::MatrixUniformName(size_t index) {
  return std::string(TRANSFORM_UNIFORM_PREFIX) + std::to_string(index);
}

std::string CoordinateBinder::VaryingName(size_t index) {
  return std::string(TRANSFORM_VARYING_PREFIX) + std::to_string(index);
}

}  // namespace tgfx
