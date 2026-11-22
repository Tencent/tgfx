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

#include "gpu/shaders/ShaderFunctionCatalog.h"
#include <cstring>
#include "core/utils/Log.h"

namespace tgfx {

bool AppendShaderFunction(const char* name, std::set<std::string>* emitted, std::string* out) {
#if defined(TGFX_HAS_FUNCTION_CATALOG)
  if (name == nullptr || name[0] == '\0' || emitted == nullptr || out == nullptr) {
    return false;
  }
  if (!emitted->insert(name).second) {
    // The function is already in the section: the call site still names it.
    return true;
  }
  for (const auto& entry : kChainFunctions) {
    if (entry.name == nullptr || std::strcmp(entry.name, name) != 0) {
      continue;
    }
    // The requires list is inferred from the function bodies, so appending the dependencies
    // first gives a text whose callees are always spelled before the call.
    const std::string requires = entry.requires != nullptr ? entry.requires : "";
    size_t start = 0;
    while (start <= requires.size()) {
      const size_t comma = requires.find(',', start);
      const std::string dep =
          requires.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      if (!dep.empty()) {
        AppendShaderFunction(dep.c_str(), emitted, out);
      }
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }
    out->append(entry.source);
    out->push_back('\n');
    return true;
  }
  LOGE("ShaderFunctionCatalog: '%s' is not in the catalog!", name);
  return false;
#else
  LOGE("ShaderFunctionCatalog: the build carries no catalog; '%s' cannot be assembled!",
       name != nullptr ? name : "");
  return false;
#endif
}

}  // namespace tgfx
