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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <type_traits>
#include <utility>
#include "core/utils/USE.h"

namespace tgfx {

static std::vector<ShaderRegistry::Factory>& GetFactories() {
  static std::vector<ShaderRegistry::Factory> factories;
  return factories;
}

struct RegisteredShader {
  PrecompiledShaderInfo info;
  ShaderRegistry::Enumerator enumerator;
};

static std::map<std::string, RegisteredShader>& GetShaderInfos() {
  static std::map<std::string, RegisteredShader> shaderInfos;
  return shaderInfos;
}

static bool SameDomain(const PermutationDomain& left, const PermutationDomain& right) {
  const auto& leftDims = left.getDimensions();
  const auto& rightDims = right.getDimensions();
  if (leftDims.size() != rightDims.size()) {
    return false;
  }
  for (size_t index = 0; index < leftDims.size(); ++index) {
    if (leftDims[index].index() != rightDims[index].index()) {
      return false;
    }
    bool same = std::visit(
        [&](const auto& dimension) {
          using Dimension = std::decay_t<decltype(dimension)>;
          const auto& other = std::get<Dimension>(rightDims[index]);
          if (std::strcmp(dimension.defineName, other.defineName) != 0 ||
              dimension.valueCount() != other.valueCount()) {
            return false;
          }
          if constexpr (std::is_same_v<Dimension, PermutationEnum>) {
            for (size_t value = 0; value < dimension.valueNames.size(); ++value) {
              if (std::strcmp(dimension.valueNames[value], other.valueNames[value]) != 0) {
                return false;
              }
            }
          }
          return true;
        },
        leftDims[index]);
    if (!same) {
      return false;
    }
  }
  return true;
}

static bool SameShaderInfo(const PrecompiledShaderInfo& left, const PrecompiledShaderInfo& right) {
  return left.name == right.name && left.vertexFile == right.vertexFile &&
         left.fragmentFile == right.fragmentFile && left.gpClassName == right.gpClassName &&
         left.declarationFile == right.declarationFile && left.textureReads == right.textureReads &&
         SameDomain(left.vertDomain, right.vertDomain) &&
         SameDomain(left.fragDomain, right.fragDomain) && SameDomain(left.gpDomain, right.gpDomain);
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

bool PermutationCompilesForBackend(const PrecompiledShaderInfo& info, uint32_t vertIndex,
                                   uint32_t fragIndex, const std::string& profileTag) {
  USE(vertIndex);
  // FBF variants (HAS_XP == 2) read the framebuffer instead of sampling a dst texture: Metal
  // through the subpassInput-to-[[color(0)]] translation, GLES through the
  // GL_EXT_shader_framebuffer_fetch dialect (WGSL cannot express the fetch at all, so webgpu is
  // excluded). Every backend that can actually reach an FBF variant has
  // frameBufferFetchSupport set in ShaderCaps (Metal: natively; GLES: through one of the fetch
  // extensions), which is what makes UsesRuntimeFrameBufferFetch in OpsCompositor return true
  // and PorterDuffXferProcessor compile without a dstTextureView, producing HAS_XP == 2.
  //
  // The Vulkan backend never wires that chain: ShaderCaps assigns frameBufferFetchSupport only
  // in the Metal / WebGPU / GL-extension branches (Vulkan falls through, staying false), the
  // render-pass code has no input-attachment binding for the subpassInput descriptor set, and
  // BlendModeNeedDstTexture therefore always resolves a dst texture on Vulkan. The vulkan bundle
  // carried the FBF variants as dead weight — loaded into the cache, unreachable at runtime.
  // They are excluded here, not deleted: the GLSL sources and the Compose* rules keep
  // enumerating them, so a future Vulkan input-attachment fetch only needs the caps flag, the
  // render-pass wiring and this exclusion removed.
  if (info.fragDomain.valueOf(fragIndex, XP_DEFINE_NAME) == XP_FRAMEBUFFER_FETCH &&
      (profileTag == "webgpu" || profileTag == "opengl" || profileTag == "vulkan")) {
    return false;
  }
  return true;
}

void ShaderRegistry::Register(Factory factory, Enumerator enumerator) {
  if (factory == nullptr || enumerator == nullptr) {
    std::fprintf(stderr, "ShaderRegistry: missing factory or enumerator\n");
    std::abort();
  }
  auto shader = factory();
  if (shader == nullptr) {
    std::fprintf(stderr, "ShaderRegistry: factory returned no shader\n");
    std::abort();
  }
  auto info = shader->info();
  if (info.name.empty()) {
    std::fprintf(stderr, "ShaderRegistry: empty family name\n");
    std::abort();
  }
  auto& shaderInfos = GetShaderInfos();
  auto existing = shaderInfos.find(info.name);
  if (existing != shaderInfos.end()) {
    // Headers register once per translation unit. Their factory lambdas have different addresses,
    // so compare the declaration and the shared enumerator, not the factory pointer.
    if (existing->second.enumerator != enumerator || !SameShaderInfo(existing->second.info, info)) {
      std::fprintf(stderr, "ShaderRegistry: conflicting declaration or enumerator for '%s'\n",
                   info.name.c_str());
      std::abort();
    }
    return;
  }
  auto name = info.name;
  shaderInfos.emplace(std::move(name), RegisteredShader{std::move(info), enumerator});
  GetFactories().push_back(factory);
}

const std::vector<ShaderRegistry::Factory>& ShaderRegistry::All() {
  return GetFactories();
}

const PrecompiledShaderInfo* ShaderRegistry::Find(const std::string& name) {
  auto& shaderInfos = GetShaderInfos();
  auto result = shaderInfos.find(name);
  return result == shaderInfos.end() ? nullptr : &result->second.info;
}

std::optional<std::set<std::pair<uint32_t, uint32_t>>> ShaderRegistry::Enumerate(
    const std::string& name) {
  const auto& shaderInfos = GetShaderInfos();
  auto result = shaderInfos.find(name);
  if (result == shaderInfos.end()) {
    return std::nullopt;
  }
  return result->second.enumerator();
}

}  // namespace tgfx
