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

#include <string>
#include <unordered_map>
#include "Uniform.h"

namespace tgfx {
class UniformBufferLayout final {
 public:
  struct Field {
    std::string name = "";
    UniformFormat format = UniformFormat::Float;
    size_t offset = 0;
    size_t size = 0;
    size_t align = 0;
  };

  explicit UniformBufferLayout(bool uboSupport) : _uboSupport(uboSupport) {
  }

  /**
   * Add a Uniform field to return its offset in UniformBuffer.
   */
  size_t add(const Uniform& uniform, size_t baseOffset);

  /**
   * Returns the total byte size that meets the alignment requirements within UniformBuffer.
   */
  size_t totalSize() const;

  /**
   * According to the given key value, the corresponding Field information is found,
   * and no nullptr is returned.
   */
  const Field* findField(const std::string& key) const;

  /**
   * Returns the number of fields in UniformBuffer.
   */
  size_t size() const;

  /**
   * Dump UniformBuffer's memory layout information is printed to the console for debugging.
   */
#if DEBUG
  void dump() const;
#endif

 private:
  struct Entry {
    size_t size;
    size_t align;
  };

  static Entry EntryOf(UniformFormat format);

  size_t alignCursor(size_t alignment) const;

  std::unordered_map<std::string, Field> fieldMap = {};
  size_t cursor = 0;
  bool _uboSupport = false;
};
}  // namespace tgfx