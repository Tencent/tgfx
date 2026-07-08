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

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

/**
 * Metadata describing a precompiled shader, including its permutation domain, source file paths,
 * and variant filtering function.
 */
struct PrecompiledShaderInfo {
  std::string name;
  std::string vertexFile;
  std::string fragmentFile;
  PermutationDomain vertDomain;
  PermutationDomain fragDomain;
  PermutationDomain gpDomain;
  std::string gpClassName;
  std::string declarationFile;
  std::function<bool(uint32_t vertIndex, uint32_t fragIndex, const std::vector<int>& vertValues,
                     const std::vector<int>& fragValues)>
      shouldCompile;
};

/**
 * Base class for all precompiled shader declarations. Each concrete shader must override info()
 * to provide its metadata.
 */
class PrecompiledShader {
 public:
  virtual ~PrecompiledShader() = default;

  virtual PrecompiledShaderInfo info() const = 0;
};

/**
 * Global registry of all precompiled shader factories. Shaders register themselves at static
 * initialization time via the TGFX_REGISTER_SHADER macro.
 */
class ShaderRegistry {
 public:
  using Factory = std::unique_ptr<PrecompiledShader> (*)();

  static void Register(Factory factory);

  static const std::vector<Factory>& All();
};

// Internal helper macros for unique name generation.
#define TGFX_CONCAT_IMPL(a, b) a##b
#define TGFX_CONCAT(a, b) TGFX_CONCAT_IMPL(a, b)

// Internal registration implementation. Creates an anonymous-namespace struct that registers the
// shader factory during static initialization.
#define TGFX_REGISTER_SHADER_IMPL(ShaderClass, counter)                                       \
  namespace {                                                                                 \
  struct TGFX_CONCAT(ShaderAutoReg_, counter) {                                               \
    TGFX_CONCAT(ShaderAutoReg_, counter)() {                                                  \
      ::tgfx::ShaderRegistry::Register([]() -> std::unique_ptr<::tgfx::PrecompiledShader> {   \
        return std::make_unique<ShaderClass>();                                               \
      });                                                                                     \
    }                                                                                         \
  };                                                                                          \
  static TGFX_CONCAT(ShaderAutoReg_, counter) TGFX_CONCAT(shaderAutoRegInstance_, counter){}; \
  }

/**
 * Registers a PrecompiledShader subclass with the global ShaderRegistry. Place this at the end
 * of the shader's .h or .cpp file, after the class definition.
 */
#define TGFX_REGISTER_SHADER(ShaderClass) TGFX_REGISTER_SHADER_IMPL(ShaderClass, __COUNTER__)

}  // namespace tgfx
