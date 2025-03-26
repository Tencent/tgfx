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

#include "MatrixColorFilter.h"
#include "core/utils/Caster.h"
#include "core/utils/MathExtra.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"

namespace tgfx {
std::shared_ptr<ColorFilter> ColorFilter::Matrix(const std::array<float, 20>& rowMajor) {
  return std::make_shared<MatrixColorFilter>(rowMajor);
}

static bool IsAlphaUnchanged(const float matrix[20]) {
  const float* srcA = matrix + 15;
  return FloatNearlyZero(srcA[0]) && FloatNearlyZero(srcA[1]) && FloatNearlyZero(srcA[2]) &&
         FloatNearlyEqual(srcA[3], 1) && FloatNearlyZero(srcA[4]);
}

MatrixColorFilter::MatrixColorFilter(const std::array<float, 20>& matrix)
    : matrix(matrix), alphaIsUnchanged(IsAlphaUnchanged(matrix.data())) {
}

bool MatrixColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto other = Caster::AsMatrixColorFilter(colorFilter);
  return other && matrix == other->matrix;
}

PlacementPtr<FragmentProcessor> MatrixColorFilter::asFragmentProcessor(Context* context) const {
  return ColorMatrixFragmentProcessor::Make(context->drawingBuffer(), matrix);
}
}  // namespace tgfx
