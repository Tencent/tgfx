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
#include <vector>

namespace tgfx {

/**
 * MangledUniforms maps base uniform names to their mangled names (e.g., "Color" → "Color_P2").
 * Used by buildCallStatement() to generate GLSL function calls with correct uniform references.
 */
class MangledUniforms {
 public:
  void add(const std::string& baseName, const std::string& mangledName) {
    uniforms_[baseName] = mangledName;
  }

  std::string get(const std::string& baseName) const {
    auto it = uniforms_.find(baseName);
    return it != uniforms_.end() ? it->second : baseName;
  }

 private:
  std::unordered_map<std::string, std::string> uniforms_;
};

/**
 * MangledVaryings maps base varying names to their mangled names.
 * For GP: maps varying base names to mangled FS input names.
 * For FP: maps coord transform indices to their varying names.
 */
class MangledVaryings {
 public:
  void add(const std::string& baseName, const std::string& mangledName) {
    varyings_[baseName] = mangledName;
  }

  void addCoordTransform(size_t index, const std::string& mangledName) {
    coordTransforms_[index] = mangledName;
  }

  std::string get(const std::string& baseName) const {
    auto it = varyings_.find(baseName);
    return it != varyings_.end() ? it->second : baseName;
  }

  std::string getCoordTransform(size_t index) const {
    auto it = coordTransforms_.find(index);
    return it != coordTransforms_.end() ? it->second : "";
  }

 private:
  std::unordered_map<std::string, std::string> varyings_;
  std::unordered_map<size_t, std::string> coordTransforms_;
};

/**
 * MangledSamplers maps base sampler names to their mangled names.
 * Also supports index-based access for texture sampler arrays.
 */
class MangledSamplers {
 public:
  void add(const std::string& baseName, const std::string& mangledName) {
    samplers_[baseName] = mangledName;
    samplerList_.push_back(mangledName);
  }

  std::string get(const std::string& baseName) const {
    auto it = samplers_.find(baseName);
    return it != samplers_.end() ? it->second : baseName;
  }

  std::string getByIndex(size_t index) const {
    return index < samplerList_.size() ? samplerList_[index] : "";
  }

  size_t count() const {
    return samplerList_.size();
  }

 private:
  std::unordered_map<std::string, std::string> samplers_;
  std::vector<std::string> samplerList_;
};

}  // namespace tgfx
