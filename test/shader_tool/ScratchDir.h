//////////////////////////////////////////////////////////////////////////////////////////////////
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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
//////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include "gtest/gtest.h"

namespace tgfx {

// A scratch directory that tests fill with shader sources or cache entries. It is created empty
// for the test and removed with it, so a test never sees the files of another.
class ScratchDir {
 public:
  explicit ScratchDir(const std::string& name)
      : path(::testing::TempDir() + "tgfx_shader_tool_" + name) {
    std::error_code code;
    std::filesystem::remove_all(path, code);
    std::filesystem::create_directories(path, code);
  }

  ~ScratchDir() {
    std::error_code code;
    std::filesystem::remove_all(path, code);
  }

  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  /// The directory itself, without a trailing slash.
  const std::string& dir() const {
    return path;
  }

  /// Writes the text to a file below the directory, creating the folders on the way.
  void write(const std::string& relativePath, const std::string& text) const {
    auto full = std::filesystem::path(path) / relativePath;
    std::error_code code;
    std::filesystem::create_directories(full.parent_path(), code);
    std::ofstream file(full, std::ios::binary | std::ios::trunc);
    file << text;
  }

 private:
  std::string path;
};

}  // namespace tgfx
