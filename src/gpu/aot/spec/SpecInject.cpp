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

void AppendSpecConstants(const std::vector<SpecFieldInfo>& fields, const SpecValues& values,
                         std::vector<std::pair<std::string, double>>* constants) {
  if (values.empty() || values.values[0] != 1) {
    return;
  }
  constants->emplace_back("0", 1.0);
  size_t next = 1;
  for (const auto& field : fields) {
    const auto words = field.arraySize == 1 ? 1u : field.arraySize * 4u;
    for (uint32_t word = 0; word < words; ++word) {
      const auto value = next < values.values.size() ? values.values[next] : 0;
      ++next;
      constants->emplace_back(std::to_string(field.firstId + word), static_cast<double>(value));
    }
  }
}

}  // namespace tgfx
