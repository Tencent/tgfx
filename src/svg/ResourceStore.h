/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/Brush.h"

namespace tgfx {

struct Resources {
  Resources() = default;
  explicit Resources(const Brush& brush);
  std::string paintColor;
  std::string filter;
  std::string mask;
};

// TODO(YGAurora) implements the feature to reuse resources
class ResourceStore {
 public:
  ResourceStore() = default;

  std::string addGradient() {
    return "gradient_" + std::to_string(gradientCount++);
  }

  std::string addPath() {
    return "path_" + std::to_string(pathCount++);
  }

  std::string addImage() {
    return "img_" + std::to_string(imageCount++);
  }

  std::string addFilter() {
    return "filter_" + std::to_string(filterCount++);
  }

  std::string addPattern() {
    return "pattern_" + std::to_string(patternCount++);
  }

  std::string addClip() {
    return "clip_" + std::to_string(clipCount++);
  }

  std::string addMask() {
    return "mask_" + std::to_string(maskCount++);
  }

 private:
  uint32_t gradientCount = 0;
  uint32_t pathCount = 0;
  uint32_t imageCount = 0;
  uint32_t patternCount = 0;
  uint32_t filterCount = 0;
  uint32_t clipCount = 0;
  uint32_t maskCount = 0;
};

}  // namespace tgfx