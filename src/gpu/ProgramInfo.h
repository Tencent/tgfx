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

#include <unordered_map>
#include "gpu/Program.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {
struct SamplerInfo {
  std::shared_ptr<Texture> texture;
  SamplerState state;
};

/**
 * This immutable object contains information needed to build a shader program and set API state for
 * a draw.
 */
class ProgramInfo {
 public:
  ProgramInfo(RenderTarget* renderTarget, GeometryProcessor* geometryProcessor,
              std::vector<FragmentProcessor*> fragmentProcessors, size_t numColorProcessors,
              XferProcessor* xferProcessor, BlendMode blendMode);

  size_t numColorFragmentProcessors() const {
    return numColorProcessors;
  }

  size_t numFragmentProcessors() const {
    return fragmentProcessors.size();
  }

  const GeometryProcessor* getGeometryProcessor() const {
    return geometryProcessor;
  }

  const FragmentProcessor* getFragmentProcessor(size_t idx) const {
    return fragmentProcessors[idx];
  }

  const XferProcessor* getXferProcessor() const;

  Swizzle getOutputSwizzle() const;

  const std::vector<Attribute>& getVertexAttributes() const {
    return geometryProcessor->vertexAttributes();
  }

  PipelineColorAttachment getPipelineColorAttachment() const;

  /**
   * Returns the index of the processor in the ProgramInfo. Returns -1 if the processor is not in
   * the ProgramInfo.
   */
  int getProcessorIndex(const Processor* processor) const;

  std::string getMangledSuffix(const Processor* processor) const;

  std::shared_ptr<Program> getProgram() const;

  std::shared_ptr<GPUBuffer> getUniformBuffer(const Program* program, size_t* vertexOffset,
                                              size_t* fragmentOffset) const;

  void bindUniformBufferAndUnloadToGPU(const Program* program,
                                       std::shared_ptr<GPUBuffer> uniformBuffer,
                                       RenderPass* renderPass, size_t vertexOffset,
                                       size_t fragmentOffset) const;

  /**
   * Sets the uniform data and texture samplers on the render pass for the given program.
   */
  void setUniformsAndSamplers(RenderPass* renderPass, Program* program) const;

  /**
   * Returns the cull face mode used for rendering.
   */
  CullMode getCullMode() const {
    return cullMode;
  }

  /**
   * Sets the cull face mode used for rendering.
   */
  void setCullMode(CullMode mode) {
    cullMode = mode;
  }

  bool getEnableDepthTest() const {
    return enableDepthTest;
  }

  void setEnableDepthTest(bool enabled) {
    enableDepthTest = enabled;
  }

 private:
  RenderTarget* renderTarget = nullptr;
  GeometryProcessor* geometryProcessor = nullptr;
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  std::unordered_map<const Processor*, int> processorIndices = {};
  // This value is also the index in fragmentProcessors where coverage processors begin.
  size_t numColorProcessors = 0;
  XferProcessor* xferProcessor = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  CullMode cullMode = CullMode::None;
  bool enableDepthTest = false;

  void updateProcessorIndices();

  std::vector<SamplerInfo> getSamplers() const;

  void updateUniformDataSuffix(UniformData* vertexUniformData, UniformData* fragmentUniformData,
                               const Processor* processor) const;
};
}  // namespace tgfx
