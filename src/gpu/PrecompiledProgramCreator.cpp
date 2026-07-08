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

#include "PrecompiledProgramCreator.h"
#include "core/utils/Log.h"
#include "gpu/PermutationMatcher.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ShaderKeyHash.h"
#include "gpu/UniformData.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

static ShaderCodeFormat FormatForBackend(Backend backend) {
  switch (backend) {
    case Backend::Vulkan:
      return ShaderCodeFormat::SPIRV;
    case Backend::Metal:
      return ShaderCodeFormat::MSL;
    case Backend::WebGPU:
      return ShaderCodeFormat::WGSL;
    default:
      return ShaderCodeFormat::GLSL;
  }
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgram(Context* context,
                                                                  const ProgramInfo* programInfo) {
  auto cache = context->precompiledShaderCache();
  if (!cache->isLoaded()) {
    return nullptr;
  }

  auto matchResult = MatchPermutation(programInfo);
  if (!matchResult) {
    return nullptr;
  }

  auto hash = ComputeShaderKeyHash(matchResult->shaderName, matchResult->permutationIndex,
                                   cache->profileTag());
  auto blob = cache->find(hash.hi, hash.lo);
  if (blob == nullptr) {
    return nullptr;
  }

  auto gpu = context->gpu();
  auto format = FormatForBackend(context->backend());

  ShaderModuleDescriptor vertexDesc = {};
  vertexDesc.format = format;
  vertexDesc.stage = ShaderStage::Vertex;
  if (format == ShaderCodeFormat::SPIRV) {
    vertexDesc.binaryData = blob->vertexData;
  } else {
    vertexDesc.code = std::string(blob->vertexData.begin(), blob->vertexData.end());
  }

  ShaderModuleDescriptor fragmentDesc = {};
  fragmentDesc.format = format;
  fragmentDesc.stage = ShaderStage::Fragment;
  if (format == ShaderCodeFormat::SPIRV) {
    fragmentDesc.binaryData = blob->fragmentData;
  } else {
    fragmentDesc.code = std::string(blob->fragmentData.begin(), blob->fragmentData.end());
  }

  auto vertexShader = gpu->createShaderModule(vertexDesc);
  if (vertexShader == nullptr) {
    LOGE("PrecompiledProgramCreator: Failed to create vertex shader module for %s[%u]",
         matchResult->shaderName.c_str(), matchResult->permutationIndex);
    return nullptr;
  }
  auto fragmentShader = gpu->createShaderModule(fragmentDesc);
  if (fragmentShader == nullptr) {
    LOGE("PrecompiledProgramCreator: Failed to create fragment shader module for %s[%u]",
         matchResult->shaderName.c_str(), matchResult->permutationIndex);
    return nullptr;
  }

  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(programInfo->getVertexAttributes());
  descriptor.vertex.bufferLayouts = {vertexLayout};
  auto& instanceAttributes = programInfo->getInstanceAttributes();
  if (!instanceAttributes.empty()) {
    VertexBufferLayout instanceLayout(instanceAttributes, VertexStepMode::Instance);
    descriptor.vertex.bufferLayouts.push_back(instanceLayout);
  }
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back(programInfo->getPipelineColorAttachment());

  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;
  if (!blob->vertexUniforms.empty()) {
    vertexUniformData = std::unique_ptr<UniformData>(new UniformData(blob->vertexUniforms));
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT,
                                  ShaderVisibility::Vertex};
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (!blob->fragmentUniforms.empty()) {
    fragmentUniformData = std::unique_ptr<UniformData>(new UniformData(blob->fragmentUniforms));
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT,
                                    ShaderVisibility::Fragment};
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  int textureBinding = 0;
  for (const auto& sampler : blob->samplers) {
    descriptor.layout.textureSamplers.emplace_back(sampler.name(), textureBinding++);
  }

  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();

  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (pipeline == nullptr) {
    LOGE("PrecompiledProgramCreator: Failed to create render pipeline for %s[%u]",
         matchResult->shaderName.c_str(), matchResult->permutationIndex);
    return nullptr;
  }

  return std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                   std::move(fragmentUniformData));
}

}  // namespace tgfx
