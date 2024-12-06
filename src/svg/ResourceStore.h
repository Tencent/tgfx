/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/FillStyle.h"

namespace tgfx {

struct Resources {
  Resources() = default;
  explicit Resources(const FillStyle& fill);
  std::string _paintColor;
  std::string _filter;
};

// TODO(YGAurora) implements the feature to reuse resources
class ResourceStore {
 public:
  ResourceStore() = default;

  std::string addGradient() {
    return "gradient_" + std::to_string(_gradientCount++);
  }

  std::string addPath() {
    return "path_" + std::to_string(_pathCount++);
  }

  std::string addImage() {
    return "img_" + std::to_string(_imageCount++);
  }

  std::string addFilter() {
    return "filter_" + std::to_string(_filterCount++);
  }

  std::string addPattern() {
    return "pattern_" + std::to_string(_patternCount++);
  }

  std::string addClip() {
    return "clip_" + std::to_string(_clipCount++);
  }

 private:
  uint32_t _gradientCount = 0;
  uint32_t _pathCount = 0;
  uint32_t _imageCount = 0;
  uint32_t _patternCount = 0;
  uint32_t _filterCount = 0;
  uint32_t _clipCount = 0;
};

}  // namespace tgfx