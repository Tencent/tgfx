/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <cstdint>
#include <set>
#include "gpu/SamplerHandle.h"
#include "gpu/ShaderVar.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {
class ProgramBuilder;
class ProgramInfo;
class ShaderCaps;

/**
 * Features that should only be enabled internally by the builders.
 */
class PrivateFeature {
 public:
  static constexpr uint32_t OESTexture = 1 << 0;
  static constexpr uint32_t FramebufferFetch = 1 << 1;
};

class ShaderBuilder {
 public:
  explicit ShaderBuilder(ProgramBuilder* builder);

  const ProgramInfo* getProgramInfo() const;

  const ShaderCaps* shaderCaps() const;

  virtual ~ShaderBuilder() = default;

  virtual ShaderStage shaderStage() const = 0;

  void setPrecisionQualifier(const std::string& precision);

  /**
   * Appends a 2D texture sampler. The vec length and swizzle order of the result depends on the
   * Texture associated with the SamplerHandle.
   */
  void appendTextureLookup(SamplerHandle samplerHandle, const std::string& coordName);

  /**
   * Called by Processors to add code to one of the shaders.
   */
  void codeAppendf(const char format[], ...);

  void codeAppend(const std::string& str);

  void addFunction(const std::string& str);

  /**
   * Appends the named catalog function and its dependencies to this shader's function section,
   * deduplicated within this shader. Returns the entry name to spell at the call site, or an empty
   * string when the catalog is absent or the name is unknown; the caller must then skip the call
   * site, or the shader would reference an undefined function.
   */
  std::string appendFunction(const char* name);

  /**
   * Emits the subset clamps that precede a texture read: the strict-constraint clamp when
   * extraSubset names a variable, then the draw-level subset clamp when subset names one.
   * Returns the name of the variable holding the clamped coordinate (coordName itself when no
   * clamp applies).
   */
  std::string clampToSubsets(const std::string& coordName, const std::string& extraSubset,
                             const std::string& subset);

  /**
   * The channel swizzle a read of the sampler's texture applies, as a GLSL suffix (".rrrr" and
   * the like), or an empty string for RGBA. Callers that spell the read through a shared function
   * append this to the result; a read and its swizzle commute through the linear blends those
   * functions perform, so one suffix on the result equals the per-read form.
   */
  std::string getSamplerSwizzleSuffix(SamplerHandle samplerHandle) const;

  std::string getMangledFunctionName(const char* baseName);

  /**
   * Combines the various parts of the shader to create a single finalized shader string.
   */
  void finalize();

  std::string shaderString();

 protected:
  class Type {
   public:
    static const uint8_t VersionDecl = 0;
    static const uint8_t Extensions = 1;
    static const uint8_t Definitions = 2;
    static const uint8_t PrecisionQualifier = 3;
    static const uint8_t Uniforms = 4;
    static const uint8_t Inputs = 5;
    static const uint8_t Outputs = 6;
    static const uint8_t Functions = 7;
    static const uint8_t Main = 8;
    static const uint8_t Code = 9;
  };

  /**
   * A general function which enables an extension in a shader if the feature bit is not present
   */
  void addFeature(uint32_t featureBit, const std::string& extensionName);

  void appendEnterIfNotEmpty(uint8_t type);

  void appendIndentationIfNeeded(const std::string& code);

  std::string getDeclarations(const std::vector<ShaderVar>& vars, ShaderStage stage) const;

  std::vector<std::string> shaderStrings;
  std::set<std::string> emittedFunctions = {};
  int tempCallIndex = 0;
  int clampedCoordIndex = 0;
  ProgramBuilder* programBuilder = nullptr;
  std::vector<ShaderVar> inputs;
  std::vector<ShaderVar> outputs;
  uint32_t features = 0;
  bool finalized = false;
  int indentation = 0;
  bool atLineStart = false;

  friend class ProgramBuilder;
  friend class UniformHandler;
};
}  // namespace tgfx
