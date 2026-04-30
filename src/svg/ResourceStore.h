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

#include <string>
#include <unordered_map>
#include "tgfx/core/Brush.h"

namespace tgfx {

struct Resources {
  Resources() = default;
  explicit Resources(const Color& color);
  std::string paintColor;
  std::string filter;
  std::string mask;
  Color colorValue;
};

class ResourceStore {
 public:
  ResourceStore() = default;

  std::string addGradient(const std::string& key = "") {
    if (!key.empty()) {
      auto it = gradientCache.find(key);
      if (it != gradientCache.end()) {
        return it->second;
      }
      std::string id = "gradient_" + std::to_string(gradientCount++);
      gradientCache[key] = id;
      return id;
    }
    return "gradient_" + std::to_string(gradientCount++);
  }

  std::string addPath(const std::string& key = "") {
    if (!key.empty()) {
      auto it = pathCache.find(key);
      if (it != pathCache.end()) {
        return it->second;
      }
      std::string id = "path_" + std::to_string(pathCount++);
      pathCache[key] = id;
      return id;
    }
    return "path_" + std::to_string(pathCount++);
  }

  std::string addImage(const std::string& key = "") {
    if (!key.empty()) {
      auto it = imageCache.find(key);
      if (it != imageCache.end()) {
        return it->second;
      }
      std::string id = "img_" + std::to_string(imageCount++);
      imageCache[key] = id;
      return id;
    }
    return "img_" + std::to_string(imageCount++);
  }

  std::string addFilter(const std::string& key = "") {
    if (!key.empty()) {
      auto it = filterCache.find(key);
      if (it != filterCache.end()) {
        return it->second;
      }
      std::string id = "filter_" + std::to_string(filterCount++);
      filterCache[key] = id;
      return id;
    }
    return "filter_" + std::to_string(filterCount++);
  }

  std::string addPattern(const std::string& key = "") {
    if (!key.empty()) {
      auto it = patternCache.find(key);
      if (it != patternCache.end()) {
        return it->second;
      }
      std::string id = "pattern_" + std::to_string(patternCount++);
      patternCache[key] = id;
      return id;
    }
    return "pattern_" + std::to_string(patternCount++);
  }

  std::string addClip(const std::string& key = "") {
    if (!key.empty()) {
      auto it = clipCache.find(key);
      if (it != clipCache.end()) {
        return it->second;
      }
      std::string id = "clip_" + std::to_string(clipCount++);
      clipCache[key] = id;
      return id;
    }
    return "clip_" + std::to_string(clipCount++);
  }

  std::string addMask(const std::string& key = "") {
    if (!key.empty()) {
      auto it = maskCache.find(key);
      if (it != maskCache.end()) {
        return it->second;
      }
      std::string id = "mask_" + std::to_string(maskCount++);
      maskCache[key] = id;
      return id;
    }
    return "mask_" + std::to_string(maskCount++);
  }

 private:
  std::unordered_map<std::string, std::string> gradientCache;
  std::unordered_map<std::string, std::string> pathCache;
  std::unordered_map<std::string, std::string> imageCache;
  std::unordered_map<std::string, std::string> patternCache;
  std::unordered_map<std::string, std::string> filterCache;
  std::unordered_map<std::string, std::string> clipCache;
  std::unordered_map<std::string, std::string> maskCache;
  
  uint32_t gradientCount = 0;
  uint32_t pathCount = 0;
  uint32_t imageCount = 0;
  uint32_t patternCount = 0;
  uint32_t filterCount = 0;
  uint32_t clipCount = 0;
  uint32_t maskCount = 0;
};

}  // namespace tgfx
