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

#pragma once

#include <cstddef>
#include <string>
#include "gpu/CoordTransform.h"
#include "gpu/SLType.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

/**
 * The single place that decides how one FP coordinate transform becomes a vertex binding. The
 * matrix the vertex stage samples through combines the transform's total matrix with the
 * geometry's uvMatrix; the varying is Float3 only when that combination keeps the perspective
 * component, and the uniform/varying names are derived here so both emission and upload agree.
 */
class CoordinateBinder {
 public:
  static Matrix CombinedMatrix(const CoordTransform& transform, const Matrix& uvMatrix);

  static bool HasPerspective(bool geometryHasPerspective, const CoordTransform& transform);

  static SLType VaryingType(bool hasPerspective);

  static std::string MatrixUniformName(size_t index);

  static std::string VaryingName(size_t index);
};

}  // namespace tgfx
