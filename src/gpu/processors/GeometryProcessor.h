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

#include <vector>
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/MangledResources.h"
#include "gpu/ShaderCallManifest.h"
#include "gpu/ShaderMacroSet.h"
#include "gpu/ShaderVar.h"
#include "gpu/UniformData.h"
#include "gpu/VaryingHandler.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/Processor.h"
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
class GeometryProcessor : public Processor {
 public:
  // Use only for easy-to-use aliases.
  using FPCoordTransformIter = FragmentProcessor::CoordTransformIter;

  const std::vector<Attribute>& vertexAttributes() const {
    return attributes;
  }

  const std::vector<Attribute>& instanceAttributes() const {
    return _instanceAttributes;
  }

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

  class FPCoordTransformHandler {
   public:
    FPCoordTransformHandler(const ProgramInfo* programInfo,
                            std::vector<ShaderVar>* transformedCoordVars)
        : iter(programInfo), transformedCoordVars(transformedCoordVars) {
    }

    const CoordTransform* nextCoordTransform() {
      return iter.next();
    }

    // 'args' are constructor params to ShaderVar.
    template <typename... Args>
    void specifyCoordsForCurrCoordTransform(Args&&... args) {
      transformedCoordVars->emplace_back(std::forward<Args>(args)...);
    }

   private:
    FragmentProcessor::CoordTransformIter iter;
    std::vector<ShaderVar>* transformedCoordVars;
  };

  /**
   * Metadata for a single FP coord transform registration, produced by registerCoordTransforms
   * and consumed by buildCoordTransformCode to emit the "TransformedCoords_i = M * vec3(uv, 1)"
   * statement after the VS call expression has been appended.
   */
  struct CoordTransformRecord {
    std::string uniformName;
    std::string varyingVsOut;
    bool hasPerspective;
    int index;
  };

  struct EmitArgs {
    EmitArgs(VertexShaderBuilder* vertBuilder, FragmentShaderBuilder* fragBuilder,
             VaryingHandler* varyingHandler, UniformHandler* uniformHandler,
             FPCoordTransformHandler* transformHandler, std::string* outputSubset)
        : vertBuilder(vertBuilder), fragBuilder(fragBuilder), varyingHandler(varyingHandler),
          uniformHandler(uniformHandler), fpCoordTransformHandler(transformHandler),
          outputSubset(outputSubset) {
    }
    VertexShaderBuilder* vertBuilder;
    FragmentShaderBuilder* fragBuilder;
    VaryingHandler* varyingHandler;
    UniformHandler* uniformHandler;
    FPCoordTransformHandler* fpCoordTransformHandler;
    std::string* outputSubset = nullptr;
    MangledVaryings* gpVaryings = nullptr;
    MangledUniforms* gpUniforms = nullptr;
    MangledSamplers* gpSamplers = nullptr;
    std::vector<CoordTransformRecord>* coordTransformRecords = nullptr;
  };

  virtual void emitCode(EmitArgs&) const = 0;

  virtual void setData(UniformData* vertexUniformData, UniformData* fragmentUniformData,
                       FPCoordTransformIter* coordTransformIter) const = 0;

  size_t numTextureSamplers() const {
    return textureSamplerCount;
  }

  std::shared_ptr<Texture> textureAt(size_t index) const {
    return onTextureAt(index);
  }

  SamplerState samplerStateAt(size_t index) const {
    return onSamplerStateAt(index);
  }

  void setTextureSamplerCount(size_t count) {
    textureSamplerCount = count;
  }

 protected:
  explicit GeometryProcessor(uint32_t classID) : Processor(classID) {
  }

  /**
   * Returns whether the uvMatrix has perspective. Subclass should override this if needed.
   */
  virtual bool hasUVPerspective() const {
    return false;
  }

  void setVertexAttributes(const Attribute* attrs, int attrCount);

  void setInstanceAttributes(const Attribute* attrs, int attrCount);

  /**
   * A helper to upload coord transform matrices in setData().
   */
  void setTransformDataHelper(const Matrix& uvMatrix, UniformData* uniformData,
                              FPCoordTransformIter* transformIter) const;

  /**
   * Phase 1 (called from emitCode): register uniform matrices and varyings for every FP coord
   * transform, and specify the coord varying for each FP via specifyCoordsForCurrCoordTransform.
   * Populates args.coordTransformRecords with metadata used later by buildCoordTransformCode,
   * and mirrors each CoordTransformMatrix_i uniform into args.gpUniforms so that subclasses'
   * buildVSCallExpr overrides can reference them by the stable key "CoordTransformMatrix_i".
   * Emits no VS code — that is deferred so ModularProgramBuilder can append the VS call
   * expression (which produces the uv varying value) before the coord transform statements that
   * consume it.
   */
  void registerCoordTransforms(EmitArgs& args, VaryingHandler* varyingHandler,
                               UniformHandler* uniformHandler) const;

  /**
   * Phase 3 (called from ModularProgramBuilder after buildVSCallExpr has been appended): returns
   * the VS statements `TransformedCoords_i = CoordTransformMatrix_i * vec3(uvCoordsExpr, 1.0);`
   * for every record registered by registerCoordTransforms. The caller is responsible for
   * appending the returned string to the vertex shader body. Pure function: no builder side
   * effects, making the emission rule replayable by offline shader generation tools.
   * uvCoordsExpr is a VS-scope expression evaluating to a vec2 (attribute name, varying vsOut
   * name, or literal).
   */
  std::string buildCoordTransformCode(EmitArgs& args, const std::string& uvCoordsExpr) const;

  // ---- Modular shader virtual methods (for ModularProgramBuilder) ----

  virtual void onBuildShaderMacros(ShaderMacroSet& /*macros*/) const {
  }

  virtual std::string shaderFunctionFile() const {
    return "";
  }

  virtual std::string buildVSCallExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& /*varyings*/) const {
    return "";
  }

  /**
   * Returns an optional FS code block that is appended at the top of the GP's FS section, before
   * buildColorCallExpr / buildCoverageCallExpr results. Used by GPs that need to precompute a
   * shared FS value (such as AtlasText sampling its atlas into a local `_atlasTexColor`) that
   * both the color and coverage manifests reference. Default returns an empty string.
   *
   * This is the FS counterpart of buildVSCallExpr: a pure function returning a text fragment,
   * no builder side effects, so the emission rule is fully replayable by offline tools.
   */
  virtual std::string buildFSPreamble(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& /*varyings*/,
                                      const MangledSamplers& /*samplers*/) const {
    return "";
  }

  /**
   * Returns the VS-scope expression that should be fed to buildCoordTransformCode as the
   * uvCoords source. This is typically an attribute name (when coord transforms operate on the
   * input position directly) or the vsOut name of a varying written by the VS call expression
   * (when coord transforms operate on a VS-computed value such as viewMatrix * position).
   *
   * All GeometryProcessor subclasses must return a non-empty, valid VS-scope expression.
   */
  virtual std::string coordTransformInputExpr(const MangledUniforms& uniforms,
                                              const MangledVaryings& varyings) const = 0;

  virtual ShaderCallManifest buildColorCallExpr(const MangledUniforms& /*uniforms*/,
                                                const MangledVaryings& /*varyings*/) const {
    return {};
  }

  virtual ShaderCallManifest buildCoverageCallExpr(const MangledUniforms& /*uniforms*/,
                                                   const MangledVaryings& /*varyings*/) const {
    return {};
  }

 private:
  virtual void onComputeProcessorKey(BytesKey*) const {
  }

  virtual std::shared_ptr<Texture> onTextureAt(size_t) const {
    return nullptr;
  }

  virtual SamplerState onSamplerStateAt(size_t) const {
    return {};
  }

  virtual void onSetTransformData(UniformData*, const CoordTransform*, int) const {
  }

  std::vector<Attribute> attributes = {};
  std::vector<Attribute> _instanceAttributes = {};
  size_t textureSamplerCount = 0;

  friend class ModularProgramBuilder;
};
}  // namespace tgfx
