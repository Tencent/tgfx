/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/spec/SpecInject.h"

namespace tgfx {

namespace {

std::string DeclText(uint32_t id, int32_t value) {
  return "TGFX_SPEC_" + std::to_string(id) + " = " + std::to_string(value);
}

// Replaces every occurrence of one declaration text. A needle like "TGFX_SPEC_1 = 0" cannot
// prefix-match "TGFX_SPEC_11 = 0": the digit run must end before the " =". Returns false when an
// occurrence was expected but none was found.
bool ReplaceAll(std::string* text, const std::string& from, const std::string& to) {
  size_t count = 0;
  for (size_t offset = text->find(from); offset != std::string::npos;
       offset = text->find(from, offset + to.size())) {
    text->replace(offset, from.size(), to);
    ++count;
  }
  return count == 1;
}

}  // namespace

std::string InjectSpecGLSL(const std::string& code, const std::vector<SpecFieldInfo>& fields,
                           const SpecValues& values) {
  if (values.empty() || values.values[0] != 1) {
    return "";
  }
  std::string text = code;
  if (!ReplaceAll(&text, DeclText(0, 0), DeclText(0, 1))) {
    return "";
  }
  size_t next = 1;
  for (const auto& field : fields) {
    const auto words = field.arraySize == 1 ? 1u : field.arraySize * 4u;
    for (uint32_t word = 0; word < words; ++word) {
      const auto id = field.firstId + word;
      const auto value = next < values.values.size() ? values.values[next] : 0;
      ++next;
      if (value == 0) {
        continue;
      }
      if (!ReplaceAll(&text, DeclText(id, 0), DeclText(id, value))) {
        return "";
      }
    }
  }
  return text;
}

}  // namespace tgfx
