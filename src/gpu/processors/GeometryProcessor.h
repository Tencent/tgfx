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
#include "gpu/Attribute.h"
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/GPUTexture.h"
#include "gpu/ShaderCaps.h"
#include "gpu/ShaderVar.h"
#include "gpu/UniformData.h"
#include "gpu/VaryingHandler.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/Processor.h"

namespace tgfx {
class GeometryProcessor : public Processor {
 public:
  // Use only for easy-to-use aliases.
  using FPCoordTransformIter = FragmentProcessor::CoordTransformIter;

  const std::vector<Attribute>& vertexAttributes() const {
    return attributes;
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

  struct EmitArgs {
    EmitArgs(VertexShaderBuilder* vertBuilder, FragmentShaderBuilder* fragBuilder,
             VaryingHandler* varyingHandler, UniformHandler* uniformHandler, const ShaderCaps* caps,
             std::string outputColor, std::string outputCoverage,
             FPCoordTransformHandler* transformHandler, std::string* outputSubset)
        : vertBuilder(vertBuilder), fragBuilder(fragBuilder), varyingHandler(varyingHandler),
          uniformHandler(uniformHandler), caps(caps), outputColor(std::move(outputColor)),
          outputCoverage(std::move(outputCoverage)), fpCoordTransformHandler(transformHandler),
          outputSubset(outputSubset) {
    }
    VertexShaderBuilder* vertBuilder;
    FragmentShaderBuilder* fragBuilder;
    VaryingHandler* varyingHandler;
    UniformHandler* uniformHandler;
    const ShaderCaps* caps;
    const std::string outputColor;
    const std::string outputCoverage;
    FPCoordTransformHandler* fpCoordTransformHandler;
    std::string* outputSubset = nullptr;
  };

  virtual void emitCode(EmitArgs&) const = 0;

  virtual void setData(UniformData* vertexUniformData, UniformData* fragmentUniformData,
                       FPCoordTransformIter* coordTransformIter) const = 0;

  size_t numTextureSamplers() const {
    return textureSamplerCount;
  }

  std::shared_ptr<GPUTexture> textureAt(size_t index) const {
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

  void setVertexAttributes(const Attribute* attrs, int attrCount);

  /**
   * A helper to upload coord transform matrices in setData().
   */
  void setTransformDataHelper(const Matrix& uvMatrix, UniformData* uniformData,
                              FPCoordTransformIter* transformIter) const;

  /**
   * Emit transformed uv coords from the vertex shader as a uniform matrix and varying per
   * coord-transform. uvCoordsVar must be a 2-component vector.
   */
  void emitTransforms(EmitArgs& args, VertexShaderBuilder* vertexBuilder,
                      VaryingHandler* varyingHandler, UniformHandler* uniformHandler,
                      const ShaderVar& uvCoordsVar) const;

 private:
  virtual void onComputeProcessorKey(BytesKey*) const {
  }

  virtual std::shared_ptr<GPUTexture> onTextureAt(size_t) const {
    return nullptr;
  }

  virtual SamplerState onSamplerStateAt(size_t) const {
    return {};
  }

  virtual void onEmitTransform(EmitArgs&, VertexShaderBuilder*, VaryingHandler*, UniformHandler*,
                               const std::string&, int) const {
  }

  virtual void onSetTransformData(UniformData*, const CoordTransform*, int) const {
  }

  std::vector<Attribute> attributes = {};
  size_t textureSamplerCount = 0;
};
}  // namespace tgfx
