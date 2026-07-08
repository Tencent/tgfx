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

namespace tgfx {

// Maps a spirv_cross base type + vector/matrix size to a UniformFormat enum value (stored as u8).
// Must stay in sync with the UniformFormat enum in src/gpu/Uniform.h.
static uint8_t MapSPIRVTypeToFormat(const spirv_cross::SPIRType& type) {
  // UniformFormat values (from Uniform.h):
  // Float=0, Float2=1, Float3=2, Float4=3,
  // Float2x2=4, Float3x3=5, Float4x4=6,
  // Int=7, Int2=8, Int3=9, Int4=10,
  // Texture2DSampler=11, TextureExternalSampler=12, Texture2DRectSampler=13
  if (type.basetype == spirv_cross::SPIRType::Float) {
    if (type.columns > 1) {
      // Matrix types
      if (type.columns == 2 && type.vecsize == 2) {
        return 4;  // Float2x2
      }
      if (type.columns == 3 && type.vecsize == 3) {
        return 5;  // Float3x3
      }
      if (type.columns == 4 && type.vecsize == 4) {
        return 6;  // Float4x4
      }
    } else {
      // Vector/scalar types
      switch (type.vecsize) {
        case 1:
          return 0;  // Float
        case 2:
          return 1;  // Float2
        case 3:
          return 2;  // Float3
        case 4:
          return 3;  // Float4
      }
    }
  } else if (type.basetype == spirv_cross::SPIRType::Int ||
             type.basetype == spirv_cross::SPIRType::UInt) {
    switch (type.vecsize) {
      case 1:
        return 7;  // Int
      case 2:
        return 8;  // Int2
      case 3:
        return 9;  // Int3
      case 4:
        return 10;  // Int4
    }
  }
  std::cerr << "WARNING: Unknown SPIR-V type (basetype=" << static_cast<int>(type.basetype)
            << " vec=" << type.vecsize << " col=" << type.columns << ")\n";
  return 0;
}

static std::vector<UniformEntry> ExtractUBOMembers(spirv_cross::Compiler& compiler,
                                                   const std::string& blockName) {
  std::vector<UniformEntry> result;
  auto resources = compiler.get_shader_resources();
  for (auto& ubo : resources.uniform_buffers) {
    auto& uboType = compiler.get_type(ubo.base_type_id);
    std::string name = compiler.get_name(ubo.id);
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
    uint8_t format = 11;  // Default: Texture2DSampler
    if (type.image.dim == spv::Dim2D) {
      format = 11;  // Texture2DSampler
    } else if (type.image.dim == spv::DimRect) {
      format = 13;  // Texture2DRectSampler
    }
    result.push_back({name, format});
  }
  return result;
}

ReflectionData ExtractReflection(const std::vector<uint32_t>& vertexSPIRV,
                                 const std::vector<uint32_t>& fragmentSPIRV) {
  ReflectionData reflection;

  if (!vertexSPIRV.empty()) {
    spirv_cross::Compiler vertCompiler(vertexSPIRV);
    reflection.vertexUniforms = ExtractUBOMembers(vertCompiler, "VertexUniformBlock");
  }

  if (!fragmentSPIRV.empty()) {
    spirv_cross::Compiler fragCompiler(fragmentSPIRV);
    reflection.fragmentUniforms = ExtractUBOMembers(fragCompiler, "FragmentUniformBlock");
    reflection.samplers = ExtractSamplers(fragCompiler);
  }

  return reflection;
}

}  // namespace tgfx
