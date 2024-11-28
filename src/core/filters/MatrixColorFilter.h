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

#pragma once

#include "core/filters/ColorFilterBase.h"

namespace tgfx {
class MatrixColorFilter : public ColorFilterBase {
 public:
  explicit MatrixColorFilter(const std::array<float, 20>& matrix);

  bool isAlphaUnchanged() const override {
    return alphaIsUnchanged;
  }

 private:
  std::array<float, 20> matrix;
  bool alphaIsUnchanged;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor() const override;
};
}  // namespace tgfx
