/////////////////////////////////////////////////////////////////////////////////////////////////
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
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "OutputFile.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace tgfx {

namespace {

bool HasContents(const std::string& path, const std::string& contents) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  file.seekg(0, std::ios::end);
  auto size = file.tellg();
  if (size < 0 || static_cast<size_t>(size) != contents.size()) {
    return false;
  }
  file.seekg(0);
  std::string existing((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return existing == contents;
}

unsigned long long ProcessId() {
#if defined(_WIN32)
  return static_cast<unsigned long long>(_getpid());
#else
  return static_cast<unsigned long long>(getpid());
#endif
}

}  // namespace

bool WriteFileIfChanged(const std::string& path, const std::string& contents, bool* changed,
                        std::string* error) {
  if (changed != nullptr) {
    *changed = false;
  }
  if (HasContents(path, contents)) {
    return true;
  }
  static std::atomic<unsigned long long> counter = {0};
  auto temp = path + ".tmp" + std::to_string(ProcessId()) + "_" + std::to_string(counter++);
  std::error_code code;
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      *error = "cannot open " + temp + " for writing";
      return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    if (!file.good()) {
      std::filesystem::remove(temp, code);
      *error = "failed while writing " + temp + " (disk full or I/O failure)";
      return false;
    }
  }
  std::filesystem::rename(temp, path, code);
  if (code) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    *error = "cannot replace " + path + ": " + code.message();
    return false;
  }
  if (changed != nullptr) {
    *changed = true;
  }
  return true;
}

}  // namespace tgfx
