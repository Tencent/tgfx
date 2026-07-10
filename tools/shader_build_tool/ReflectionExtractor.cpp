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

#include "ReflectionExtractor.h"
#include <iostream>
#include <spirv_cross.hpp>
#include "gpu/Uniform.h"

namespace tgfx {

static uint8_t MapSPIRVTypeToFormat(const spirv_cross::SPIRType& type) {
  if (type.basetype == spirv_cross::SPIRType::Float) {
    if (type.columns > 1) {
      if (type.columns == 2 && type.vecsize == 2) {
        return static_cast<uint8_t>(UniformFormat::Float2x2);
      }
      if (type.columns == 3 && type.vecsize == 3) {
        return static_cast<uint8_t>(UniformFormat::Float3x3);
      }
      if (type.columns == 4 && type.vecsize == 4) {
        return static_cast<uint8_t>(UniformFormat::Float4x4);
      }
    } else {
      switch (type.vecsize) {
        case 1:
          return static_cast<uint8_t>(UniformFormat::Float);
        case 2:
          return static_cast<uint8_t>(UniformFormat::Float2);
        case 3:
          return static_cast<uint8_t>(UniformFormat::Float3);
        case 4:
          return static_cast<uint8_t>(UniformFormat::Float4);
      }
    }
  } else if (type.basetype == spirv_cross::SPIRType::Int ||
             type.basetype == spirv_cross::SPIRType::UInt) {
    switch (type.vecsize) {
      case 1:
        return static_cast<uint8_t>(UniformFormat::Int);
      case 2:
        return static_cast<uint8_t>(UniformFormat::Int2);
      case 3:
        return static_cast<uint8_t>(UniformFormat::Int3);
      case 4:
        return static_cast<uint8_t>(UniformFormat::Int4);
    }
  }
  std::cerr << "WARNING: Unknown SPIR-V type (basetype=" << static_cast<int>(type.basetype)
            << " vec=" << type.vecsize << " col=" << type.columns << ")\n";
  return static_cast<uint8_t>(UniformFormat::Float);
}

static std::vector<UniformEntry> ExtractUBOMembers(spirv_cross::Compiler& compiler,
                                                   const std::string& blockName) {
  std::vector<UniformEntry> result;
  auto resources = compiler.get_shader_resources();
  for (auto& ubo : resources.uniform_buffers) {
    auto& uboType = compiler.get_type(ubo.base_type_id);
    std::string name = compiler.get_name(ubo.id);
    if (name.empty()) {
      name = compiler.get_name(ubo.base_type_id);
    }
    if (name.empty()) {
      name = compiler.get_fallback_name(ubo.id);
    }
    if (!blockName.empty() && name != blockName) {
      continue;
    }
    for (uint32_t i = 0; i < uboType.member_types.size(); i++) {
      auto& memberType = compiler.get_type(uboType.member_types[i]);
      std::string memberName = compiler.get_member_name(ubo.base_type_id, i);
      UniformEntry entry;
      entry.name = memberName;
      entry.format = MapSPIRVTypeToFormat(memberType);
      result.push_back(entry);
    }
  }
  return result;
}

static std::vector<UniformEntry> ExtractSamplers(spirv_cross::Compiler& compiler) {
  std::vector<UniformEntry> result;
  auto resources = compiler.get_shader_resources();
  for (auto& sampler : resources.sampled_images) {
    std::string name = compiler.get_name(sampler.id);
    auto& type = compiler.get_type(sampler.type_id);
    uint8_t format = static_cast<uint8_t>(UniformFormat::Texture2DSampler);
    if (type.image.dim == spv::DimRect) {
      format = static_cast<uint8_t>(UniformFormat::Texture2DRectSampler);
    }
    result.push_back({name, format});
  }
  return result;
}

ReflectionResult ExtractReflection(const std::vector<uint32_t>& vertexSPIRV,
                                   const std::vector<uint32_t>& fragmentSPIRV) {
  ReflectionResult result;

  if (!vertexSPIRV.empty()) {
    spirv_cross::Compiler vertCompiler(vertexSPIRV);
    result.vertexReflection.uniforms = ExtractUBOMembers(vertCompiler, "VertexUniformBlock");
  }

  if (!fragmentSPIRV.empty()) {
    spirv_cross::Compiler fragCompiler(fragmentSPIRV);
    result.fragmentReflection.uniforms = ExtractUBOMembers(fragCompiler, "FragmentUniformBlock");
    result.fragmentReflection.samplers = ExtractSamplers(fragCompiler);
  }

  return result;
}

}  // namespace tgfx
