/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>

namespace tgfx {

/// The std140 column packing of a row-major 3x3 matrix: three vec4 columns with the fourth lane
/// padding. Shared by the chain encoder's identity tests and the binder's uniform writes so the
/// two sides cannot disagree about the layout.
inline std::array<float, 12> Mat3ToStd140ColumnMajor(const std::array<float, 9>& values) {
  return {values[0], values[3], values[6], 0.0f,      values[1], values[4],
          values[7], 0.0f,      values[2], values[5], values[8], 0.0f};
}

/// Returns true when the row-major 3x3 matrix is the identity.
inline bool IsIdentityMat3RowMajor(const std::array<float, 9>& values) {
  return values[0] == 1.f && values[1] == 0.f && values[2] == 0.f && values[3] == 0.f &&
         values[4] == 1.f && values[5] == 0.f && values[6] == 0.f && values[7] == 0.f &&
         values[8] == 1.f;
}

}  // namespace tgfx
