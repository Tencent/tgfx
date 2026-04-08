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

#include <map>
#include <string>
#include "tgfx/core/BytesKey.h"

namespace tgfx {

/**
 * ShaderMacroSet collects #define macros for shader variant control.
 * Processors fill this via onBuildShaderMacros(), and ProgramBuilder uses toPreamble()
 * to generate the #define preamble injected at the top of the shader source.
 *
 * Naming convention: TGFX_{ProcessorAbbrev}_{Feature}
 * Examples: TGFX_GP_ELLIPSE_STROKE, TGFX_TE_TEXTURE_MODE, TGFX_UBGC_INTERVAL_COUNT
 */
class ShaderMacroSet {
 public:
  void define(const std::string& name) {
    macros_[name] = "1";
  }

  void define(const std::string& name, int value) {
    macros_[name] = std::to_string(value);
  }

  void define(const std::string& name, const std::string& value) {
    macros_[name] = value;
  }

  bool isDefined(const std::string& name) const {
    return macros_.count(name) > 0;
  }

  int getInt(const std::string& name, int defaultValue = 0) const {
    auto it = macros_.find(name);
    if (it == macros_.end()) return defaultValue;
    return std::stoi(it->second);
  }

  /**
   * Generate #define preamble string for injection into shader header.
   * std::map traversal is ordered, guaranteeing deterministic output.
   */
  std::string toPreamble() const {
    std::string preamble;
    for (const auto& [name, value] : macros_) {
      preamble += "#define " + name + " " + value + "\n";
    }
    return preamble;
  }

  /**
   * Write macro state to BytesKey for pipeline cache key computation.
   */
  void writeToKey(BytesKey* key) const {
    for (const auto& [name, value] : macros_) {
      key->write(static_cast<uint32_t>(std::hash<std::string>{}(name)));
      key->write(static_cast<uint32_t>(std::hash<std::string>{}(value)));
    }
  }

  bool empty() const {
    return macros_.empty();
  }

 private:
  std::map<std::string, std::string> macros_;
};

}  // namespace tgfx
