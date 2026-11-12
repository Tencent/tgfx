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
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include "core/utils/USE.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"

namespace tgfx {

namespace {

/// The name-keyed view the matcher and the reports resolve through. The per-family enumerators
// that used to live here had no runtime caller once the bundle became variant-id keyed: the tool
// compiles EnumerateVariants directly, and the reachable-set reference the tests compare against
// lives with those tests.
const std::map<std::string, PrecompiledShaderInfo>& ShaderInfos() {
  static const std::map<std::string, PrecompiledShaderInfo> infos = [] {
    std::map<std::string, PrecompiledShaderInfo> result;
    for (size_t index = 0; index < FamilyCount(); ++index) {
      auto info = AssembledFamilyInfo(Families()[index]);
      auto name = info.name;
      result.emplace(std::move(name), std::move(info));
    }
    return result;
  }();
  return infos;
}

}  // namespace

bool PrecompiledShaderInfo::IsBuildable(uint32_t vertIndex, uint32_t fragIndex) const {
  if (vertIndex >= vertDomain.totalCount() || fragIndex >= fragDomain.totalCount()) {
    return false;
  }
  auto vertValues = vertDomain.decode(vertIndex);
  auto fragValues = fragDomain.decode(fragIndex);
  return MirroredDimsAgree(vertDomain, fragDomain, vertValues, fragValues);
}

bool PermutationCompilesForBackend(const PrecompiledShaderInfo& info, uint32_t vertIndex,
                                   uint32_t fragIndex, ShaderProfile profile) {
  USE(vertIndex);
  if (info.fragDomain.valueOf(fragIndex, XP_DEFINE_NAME) == XP_FRAMEBUFFER_FETCH &&
      !ProfileSupportsFramebufferFetch(profile)) {
    return false;
  }
  return true;
}

const PrecompiledShaderInfo* FindShaderInfo(const std::string& name) {
  const auto& infos = ShaderInfos();
  auto result = infos.find(name);
  return result == infos.end() ? nullptr : &result->second;
}

}  // namespace tgfx
