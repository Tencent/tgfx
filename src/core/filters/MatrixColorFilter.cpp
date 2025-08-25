/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
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

std::optional<Color> MatrixColorFilter::tryFilterColor(const Color& input) const {
  Color transformedColor;
  transformedColor.red = matrix[0] * input.red + matrix[1] * input.green + matrix[2] * input.blue +
                         matrix[3] * input.alpha + matrix[4];
  transformedColor.green = matrix[5] * input.red + matrix[6] * input.green +
                           matrix[7] * input.blue + matrix[8] * input.alpha + matrix[9];
  transformedColor.blue = matrix[10] * input.red + matrix[11] * input.green +
                          matrix[12] * input.blue + matrix[13] * input.alpha + matrix[14];
  transformedColor.alpha = matrix[15] * input.red + matrix[16] * input.green +
                           matrix[17] * input.blue + matrix[18] * input.alpha + matrix[19];

  transformedColor.red = std::clamp(transformedColor.red, 0.0f, 1.0f);
  transformedColor.green = std::clamp(transformedColor.green, 0.0f, 1.0f);
  transformedColor.blue = std::clamp(transformedColor.blue, 0.0f, 1.0f);
  transformedColor.alpha = std::clamp(transformedColor.alpha, 0.0f, 1.0f);
  return std::make_optional<Color>(transformedColor);
}

bool MatrixColorFilter::isEqual(const ColorFilter* colorFilter) const {
  auto type = Types::Get(colorFilter);
  if (type != Types::ColorFilterType::Matrix) {
    return false;
  }
  auto other = static_cast<const MatrixColorFilter*>(colorFilter);
  return matrix == other->matrix;
}

PlacementPtr<FragmentProcessor> MatrixColorFilter::asFragmentProcessor(Context* context) const {
  return ColorMatrixFragmentProcessor::Make(context->drawingBuffer(), matrix);
}
}  // namespace tgfx
