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

#include "gpu/shaders/PrecompiledShader.h"
#include <map>

namespace tgfx {

static std::vector<ShaderRegistry::Factory>& GetFactories() {
  static std::vector<ShaderRegistry::Factory> factories;
  return factories;
}

static std::map<std::string, PrecompiledShaderInfo>& GetShaderInfos() {
  static std::map<std::string, PrecompiledShaderInfo> shaderInfos;
  return shaderInfos;
}

bool IsBuildablePermutation(const PrecompiledShaderInfo& info, uint32_t vertIndex,
                            uint32_t fragIndex) {
  if (vertIndex >= info.vertDomain.totalCount() || fragIndex >= info.fragDomain.totalCount()) {
    return false;
  }
  auto vertValues = info.vertDomain.decode(vertIndex);
  auto fragValues = info.fragDomain.decode(fragIndex);
  return MirroredDimsAgree(info.vertDomain, info.fragDomain, vertValues, fragValues);
}

void ShaderRegistry::Register(Factory factory) {
  auto shader = factory();
  if (shader == nullptr) {
    return;
  }
  auto info = shader->info();
  auto name = info.name;
  auto result = GetShaderInfos().emplace(std::move(name), std::move(info));
  if (result.second) {
    GetFactories().push_back(factory);
  }
}

const std::vector<ShaderRegistry::Factory>& ShaderRegistry::All() {
  return GetFactories();
}

const PrecompiledShaderInfo* ShaderRegistry::Find(const std::string& name) {
  auto& shaderInfos = GetShaderInfos();
  auto result = shaderInfos.find(name);
  return result == shaderInfos.end() ? nullptr : &result->second;
}

}  // namespace tgfx
