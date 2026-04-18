/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <optional>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
class AtlasTextGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<AtlasTextGeometryProcessor> Make(BlockAllocator* allocator,
                                                       std::shared_ptr<TextureProxy> textureProxy,
                                                       AAType aa,
                                                       std::optional<PMColor> commonColor,
                                                       const SamplingOptions& sampling);
  std::string name() const override {
    return "AtlasTextGeometryProcessor";
  }

  SamplerState onSamplerStateAt(size_t) const override {
    return samplerState;
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  AtlasTextGeometryProcessor(std::shared_ptr<TextureProxy> textureProxy, AAType aa,
                             std::optional<PMColor> commonColor, const SamplingOptions& sampling);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (aa == AAType::Coverage) {
      macros.define("TGFX_GP_ATLAS_COVERAGE_AA");
    }
    if (commonColor.has_value()) {
      macros.define("TGFX_GP_ATLAS_COMMON_COLOR");
    }
    if (textureProxy->isAlphaOnly()) {
      macros.define("TGFX_GP_ATLAS_ALPHA_ONLY");
    }
  }

  std::string shaderFunctionFile() const override {
    return "geometry/atlas_text_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& uniforms,
                              const MangledVaryings& varyings) const override {
    std::string code = "highp vec2 position;\n";
    code += "TGFX_AtlasTextGP_VS(" + std::string(position.name()) + ", " +
            std::string(maskCoord.name()) + ", " + uniforms.get("atlasSizeInv");
    if (aa == AAType::Coverage) {
      code += ", " + std::string(coverage.name()) + ", " + varyings.get("Coverage");
    }
    if (!commonColor.has_value()) {
      code += ", " + std::string(color.name()) + ", " + varyings.get("Color");
    }
    code += ", " + varyings.get("textureCoords") + ", position);\n";
    code += "gl_Position = TGFX_NormalizePosition(position);\n";
    return code;
  }

  ShaderCallManifest buildColorCallExpr(const MangledUniforms& uniforms,
                                        const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpColor";
    auto texColor = uniforms.get("atlasTexColor");
    std::string code;
    if (textureProxy->isAlphaOnly()) {
      if (commonColor.has_value()) {
        code += "vec4 gpColor = " + uniforms.get("Color") + ";\n";
      } else {
        code += "vec4 gpColor = " + varyings.get("Color") + ";\n";
      }
    } else {
      code +=
          "vec4 gpColor = clamp(vec4(" + texColor + ".rgb/" + texColor + ".a, 1.0), 0.0, 1.0);\n";
    }
    result.statement = code;
    return result;
  }

  ShaderCallManifest buildCoverageCallExpr(const MangledUniforms& /*uniforms*/,
                                           const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpCoverage";
    std::string code;
    if (textureProxy->isAlphaOnly()) {
      code += "vec4 gpCoverage = vec4(_atlasTexColor.a);\n";
    } else {
      if (aa == AAType::Coverage) {
        auto cov = varyings.get("Coverage");
        code += "vec4 gpCoverage = vec4(_atlasTexColor.a) * vec4(" + cov + ");\n";
      } else {
        code += "vec4 gpCoverage = vec4(_atlasTexColor.a);\n";
      }
    }
    result.statement = code;
    return result;
  }

  std::shared_ptr<Texture> onTextureAt(size_t index) const override {
    DEBUG_ASSERT(index < textures.size());
    return textures[index];
  }

  Attribute position;  // May contain coverage as last channel
  Attribute coverage;
  Attribute maskCoord;
  Attribute color;

  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  AAType aa = AAType::None;
  std::optional<PMColor> commonColor = std::nullopt;
  std::vector<std::shared_ptr<Texture>> textures;
  SamplerState samplerState = {};
};
}  // namespace tgfx
