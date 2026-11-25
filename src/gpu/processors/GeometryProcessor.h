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
#include "gpu/ShaderCaps.h"
#include "gpu/ShaderVar.h"
#include "gpu/UniformData.h"
#include "gpu/VaryingHandler.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/Processor.h"
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
class ProgramIRBuilder;

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
                            std::vector<CoordValue>* transformedCoordVars)
        : iter(programInfo), transformedCoordVars(transformedCoordVars) {
    }

    const CoordTransform* nextCoordTransform() {
      return iter.next();
    }

    void specifyCoordsForCurrCoordTransform(const std::string& name, SLType type,
                                            VaryingID varying) {
      transformedCoordVars->push_back({name, type, varying});
    }

   private:
    FragmentProcessor::CoordTransformIter iter;
    std::vector<CoordValue>* transformedCoordVars;
  };

  struct EmitArgs {
    EmitArgs(VertexShaderBuilder* vertBuilder, FragmentShaderBuilder* fragBuilder,
             VaryingHandler* varyingHandler, UniformHandler* uniformHandler, const ShaderCaps* caps,
             std::string outputColor, std::string outputCoverage,
             FPCoordTransformHandler* transformHandler, std::string* outputSubset,
             ProgramIRBuilder* irBuilder = nullptr)
        : vertBuilder(vertBuilder), fragBuilder(fragBuilder), varyingHandler(varyingHandler),
          uniformHandler(uniformHandler), caps(caps), outputColor(std::move(outputColor)),
          outputCoverage(std::move(outputCoverage)), fpCoordTransformHandler(transformHandler),
          outputSubset(outputSubset), irBuilder(irBuilder) {
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
    ProgramIRBuilder* irBuilder = nullptr;
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
   * Emit transformed uv coords from the vertex shader as a uniform matrix and varying per
   * coord-transform. uvCoordsVar must be a 2-component vector.
   */
  void emitTransforms(EmitArgs& args, VertexShaderBuilder* vertexBuilder,
                      VaryingHandler* varyingHandler, UniformHandler* uniformHandler,
                      const ShaderVar& uvCoordsVar) const;

  /**
   * The IR side of storePosition(): gl_Position = vec4(position.xy * rtAdjust.xz + rtAdjust.yw,
   * 0, 1). The caller must run in the vertex stage; the RTAdjust uniform is declared by the
   * program builder before the processors.
   * @param position The position attribute the vertex stage stores.
   */
  void emitPositionIR(EmitArgs& args, const ShaderVar& position) const;

  /**
   * The IR side of storeVarying(): registers the varying on the IR interface (the name is
   * deduplicated, so a later registration from the varying handler is a no-op) and stores the
   * source attribute's value into it. The caller must run in the vertex stage.
   * @param varying The varying the text route declared through the varying handler.
   * @param source The vertex attribute feeding the varying.
   */
  void emitVaryingIR(EmitArgs& args, const Varying& varying, const ShaderVar& source) const;

 private:
  virtual void onComputeProcessorKey(BytesKey*) const {
  }

  virtual std::shared_ptr<Texture> onTextureAt(size_t) const {
    return nullptr;
  }

  virtual SamplerState onSamplerStateAt(size_t) const {
    return {};
  }

  virtual void onEmitTransform(EmitArgs&, VertexShaderBuilder*, VaryingHandler*, UniformHandler*,
                               const std::string&, bool, int) const {
  }

  virtual void onSetTransformData(UniformData*, const CoordTransform*, int) const {
  }

  std::vector<Attribute> attributes = {};
  std::vector<Attribute> _instanceAttributes = {};
  size_t textureSamplerCount = 0;
};
}  // namespace tgfx
