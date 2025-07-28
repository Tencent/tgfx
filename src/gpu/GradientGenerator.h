/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <vector>
#include "tgfx/core/Color.h"
#include "tgfx/core/ImageGenerator.h"

namespace tgfx {

class GradientGenerator : public ImageGenerator {
 public:
  GradientGenerator(const Color* colors, const float* positions, int count);

  bool isAlphaOnly() const override {
    return false;
  }

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool) const override;

 private:
  std::vector<Color> colors = {};
  std::vector<float> positions = {};
};
}  // namespace tgfx
