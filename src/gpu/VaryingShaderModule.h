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

#pragma once

#include <unordered_map>
#include <utility>
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

/**
 * Internal base class for shader modules that declare a varying interface (vertex `out` or
 * fragment `in` declarations). The varying interface is only consumed by the backend render
 * pipelines for cross-stage validation, so it lives in this src/-side base class instead of the
 * public ShaderModule API.
 */
class VaryingShaderModule : public ShaderModule {
 public:
  const std::unordered_map<std::string, int>& varyingDecls() const {
    return _varyingDecls;
  }

 protected:
  explicit VaryingShaderModule(std::unordered_map<std::string, int> varyingDecls)
      : _varyingDecls(std::move(varyingDecls)) {
  }

 private:
  std::unordered_map<std::string, int> _varyingDecls = {};
};

}  // namespace tgfx
