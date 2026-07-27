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
#include <sstream>
#include "core/utils/Log.h"
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/GlobalCache.h"
#include "gpu/PermutationMatcher.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProgramSignature.h"
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

static ShaderArtifactOrigin ArtifactOriginForBackend(
    Backend backend, const ShaderModuleDescriptor& vertexDescriptor,
    const ShaderModuleDescriptor& fragmentDescriptor) {
  auto hasOfflineBinary =
      !vertexDescriptor.binaryData.empty() && !fragmentDescriptor.binaryData.empty();
  if ((backend == Backend::Vulkan || backend == Backend::Metal) && hasOfflineBinary) {
    return ShaderArtifactOrigin::OfflineBinary;
  }
  return ShaderArtifactOrigin::OfflineSource;
}

static void AppendFragmentProcessor(std::stringstream& stream, const FragmentProcessor* processor) {
  stream << processor->name();
  if (processor->numChildProcessors() == 0) {
    return;
  }
  stream << "(";
  for (size_t i = 0; i < processor->numChildProcessors(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, processor->childProcessor(i));
  }
  stream << ")";
}

std::string BuildEffectSignature(const ProgramInfo* programInfo) {
  std::stringstream stream;
  stream << "GP=" << programInfo->getGeometryProcessor()->name() << ";ColorFP=[";
  for (size_t i = 0; i < programInfo->numColorFragmentProcessors(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, programInfo->getFragmentProcessor(i));
  }
  stream << "];CoverageFP=[";
  for (size_t i = programInfo->numColorFragmentProcessors();
       i < programInfo->numFragmentProcessors(); ++i) {
    if (i > programInfo->numColorFragmentProcessors()) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, programInfo->getFragmentProcessor(i));
  }
  stream << "];XP=" << programInfo->getXferProcessor()->name();
  return stream.str();
}

static void AppendStencilSignature(std::stringstream& stream, const char* name,
                                   const StencilDescriptor& stencil) {
  stream << ";" << name << "Compare=" << static_cast<uint32_t>(stencil.compare) << ";" << name
         << "Fail=" << static_cast<uint32_t>(stencil.failOp) << ";" << name
         << "DepthFail=" << static_cast<uint32_t>(stencil.depthFailOp) << ";" << name
         << "Pass=" << static_cast<uint32_t>(stencil.passOp);
}

static std::string BuildPipelineSignature(const ProgramInfo* programInfo) {
  std::stringstream stream;
  const auto colorAttachment = programInfo->getPipelineColorAttachment();
  const auto& depthStencil = programInfo->getDepthStencil();
  stream << BuildEffectSignature(programInfo)
         << ";Swizzle=" << programInfo->getOutputSwizzle().c_str()
         << ";Format=" << static_cast<uint32_t>(colorAttachment.format)
         << ";BlendEnable=" << static_cast<uint32_t>(colorAttachment.blendEnable)
         << ";SrcColorBlend=" << static_cast<uint32_t>(colorAttachment.srcColorBlendFactor)
         << ";DstColorBlend=" << static_cast<uint32_t>(colorAttachment.dstColorBlendFactor)
         << ";ColorBlendOp=" << static_cast<uint32_t>(colorAttachment.colorBlendOp)
         << ";SrcAlphaBlend=" << static_cast<uint32_t>(colorAttachment.srcAlphaBlendFactor)
         << ";DstAlphaBlend=" << static_cast<uint32_t>(colorAttachment.dstAlphaBlendFactor)
         << ";AlphaBlendOp=" << static_cast<uint32_t>(colorAttachment.alphaBlendOp)
         << ";Samples=" << programInfo->getSampleCount()
         << ";Cull=" << static_cast<uint32_t>(programInfo->getCullMode())
         << ";ColorWriteMask=" << colorAttachment.colorWriteMask
         << ";DepthStencilFormat=" << static_cast<uint32_t>(depthStencil.format)
         << ";DepthCompare=" << static_cast<uint32_t>(depthStencil.depthCompare)
         << ";DepthWrite=" << static_cast<uint32_t>(depthStencil.depthWriteEnabled)
         << ";StencilReadMask=" << depthStencil.stencilReadMask
         << ";StencilWriteMask=" << depthStencil.stencilWriteMask;
  AppendStencilSignature(stream, "StencilFront", depthStencil.stencilFront);
  AppendStencilSignature(stream, "StencilBack", depthStencil.stencilBack);
  return stream.str();
}

static PrecompiledFallbackRecord MakeFallbackRecord(
    const PrecompiledShaderCache* cache, const ProgramInfo* programInfo,
    const PermutationMatchResult* matchResult = nullptr) {
  PrecompiledFallbackRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.effectSignature = BuildEffectSignature(programInfo);
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  if (matchResult != nullptr) {
    record.shaderName = matchResult->shaderName;
    record.vertPermutationIndex = matchResult->vertPermutationIndex;
    record.fragPermutationIndex = matchResult->fragPermutationIndex;
  }
  return record;
}

static PrecompiledHitRecord MakeHitRecord(const PrecompiledShaderCache* cache,
                                          const ProgramInfo* programInfo,
                                          const PermutationMatchResult& matchResult) {
  PrecompiledHitRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.effectSignature = BuildEffectSignature(programInfo);
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  record.shaderName = matchResult.shaderName;
  record.vertPermutationIndex = matchResult.vertPermutationIndex;
  record.fragPermutationIndex = matchResult.fragPermutationIndex;
  return record;
}

static PrecompiledFallbackReason ToFallbackReason(PermutationMatchFailure failure) {
  switch (failure) {
    case PermutationMatchFailure::UnsupportedOutputSwizzle:
      return PrecompiledFallbackReason::UnsupportedOutputSwizzle;
    case PermutationMatchFailure::NoMatchingRule:
      return PrecompiledFallbackReason::NoMatchingRule;
    case PermutationMatchFailure::None:
      return PrecompiledFallbackReason::Unspecified;
  }
  return PrecompiledFallbackReason::Unspecified;
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateDecomposedProgram(
    Context* context, const ProgramInfo* programInfo) {
  auto cache = context->precompiledShaderCache();
  // Gated off by default: unverified kernels never reach production. Every draw falls back to the
  // plain route until decomposition is explicitly enabled (e.g. during cross-validation).
  if (!cache->decompositionEnabled() || !cache->isLoaded()) {
    return nullptr;
  }
  // Lower the color fragment-processor chain into a typed effect graph. Each FP lowers its own
  // subtree via lowerToAOT(); any processor without an AOT lowering returns false, which aborts the
  // whole decomposition and falls back to the plain route — a built-in safety gate against unknown
  // effects.
  std::vector<const FragmentProcessor*> colorProcessors;
  auto colorCount = programInfo->numColorFragmentProcessors();
  colorProcessors.reserve(colorCount);
  for (size_t i = 0; i < colorCount; ++i) {
    colorProcessors.push_back(programInfo->getFragmentProcessor(i));
  }
  AOTEffectGraph graph = {};
  if (!AOTEffectDecomposer::Lower(colorProcessors, &graph)) {
    return nullptr;
  }
  // Semantic guard (review #4): reject chains whose fusion would change pixels (e.g. a ColorMatrix
  // that affects transparent black). Rejection falls back to the plain route.
  if (!AOTEffectDecomposer::ValidateForFusion(graph)) {
    return nullptr;
  }
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::PreferFusion, &plan)) {
    return nullptr;
  }
  // The plan is valid, but the PointwiseChainShader (with its Opcode uniform layout) does not yet
  // exist in the bundle (review #3, stage 2 remaining work). Until that shader and its bundle
  // entries land, we must not fabricate a program with a mismatched layout: return nullptr so the
  // atomic fallback in ProgramInfo::getProgram() serves this draw via the plain route. The
  // decomposition and validation logic above is exercised now; only the artifact mapping is
  // pending.
  return nullptr;
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgram(Context* context,
                                                                  const ProgramInfo* programInfo) {
  auto cache = context->precompiledShaderCache();
  cache->recordAOTStage(PrecompiledAOTStage::Attempt);
  if (!cache->isLoaded()) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::CacheNotLoaded,
                              MakeFallbackRecord(cache, programInfo));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::CacheAvailable);

  PermutationMatchFailure matchFailure = PermutationMatchFailure::None;
  auto matchResult = MatchPermutation(programInfo, &matchFailure);
  if (!matchResult) {
    auto reason = ToFallbackReason(matchFailure);
    cache->recordArtifactMiss(reason, MakeFallbackRecord(cache, programInfo));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PermutationMatched);

  auto vertHash = ComputeVertexKeyHash(matchResult->shaderName, matchResult->vertPermutationIndex,
                                       cache->profileTag());
  auto fragHash = ComputeFragmentKeyHash(matchResult->shaderName, matchResult->fragPermutationIndex,
                                         cache->profileTag());

  auto vertBlob = cache->findVertex(vertHash.hi, vertHash.lo);
  if (vertBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::VertexArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledShaderMiss: vert blob not found for %s[vert=%u]",
         matchResult->shaderName.c_str(), matchResult->vertPermutationIndex);
    return nullptr;
  }
  auto fragBlob = cache->findFragment(fragHash.hi, fragHash.lo);
  if (fragBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::FragmentArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledShaderMiss: frag blob not found for %s[frag=%u]",
         matchResult->shaderName.c_str(), matchResult->fragPermutationIndex);
    return nullptr;
  }
  cache->recordArtifactHit();

  auto gpu = context->gpu();
  auto format = FormatForBackend(context->backend());

  ShaderModuleDescriptor vertexDesc = {};
  vertexDesc.format = format;
  vertexDesc.stage = ShaderStage::Vertex;
  if (format == ShaderCodeFormat::SPIRV || format == ShaderCodeFormat::MSL) {
    vertexDesc.binaryData = vertBlob->data;
  } else {
    vertexDesc.code = std::string(vertBlob->data.begin(), vertBlob->data.end());
  }

  ShaderModuleDescriptor fragmentDesc = {};
  fragmentDesc.format = format;
  fragmentDesc.stage = ShaderStage::Fragment;
  if (format == ShaderCodeFormat::SPIRV || format == ShaderCodeFormat::MSL) {
    fragmentDesc.binaryData = fragBlob->data;
  } else {
    fragmentDesc.code = std::string(fragBlob->data.begin(), fragBlob->data.end());
  }

  auto vertexShader = gpu->createShaderModule(vertexDesc);
  if (vertexShader == nullptr) {
    cache->recordFailure(PrecompiledFallbackReason::VertexModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledProgramCreator: Failed to create vertex shader module for %s[vert=%u]",
         matchResult->shaderName.c_str(), matchResult->vertPermutationIndex);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::VertexModuleCreated);
  auto fragmentShader = gpu->createShaderModule(fragmentDesc);
  if (fragmentShader == nullptr) {
    cache->recordFailure(PrecompiledFallbackReason::FragmentModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledProgramCreator: Failed to create fragment shader module for %s[frag=%u]",
         matchResult->shaderName.c_str(), matchResult->fragPermutationIndex);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::FragmentModuleCreated);

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
  if (!vertBlob->uniforms.empty()) {
    vertexUniformData = std::unique_ptr<UniformData>(new UniformData(vertBlob->uniforms));
    vertexUniformData->skipSuffix = true;
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT,
                                  ShaderVisibility::Vertex};
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (!fragBlob->uniforms.empty()) {
    fragmentUniformData = std::unique_ptr<UniformData>(new UniformData(fragBlob->uniforms));
    fragmentUniformData->skipSuffix = true;
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT,
                                    ShaderVisibility::Fragment};
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  int textureBinding = 0;
  for (const auto& sampler : fragBlob->samplers) {
    descriptor.layout.textureSamplers.emplace_back(sampler.name(), textureBinding++);
  }

  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();

  auto pipeline = gpu->createRenderPipeline(descriptor);
  context->globalCache()->recordRuntimePipelineCreation(pipeline != nullptr);
  if (pipeline == nullptr) {
    LOGE("PrecompiledProgramCreator: Failed to create render pipeline for %s[vert=%u,frag=%u]",
         matchResult->shaderName.c_str(), matchResult->vertPermutationIndex,
         matchResult->fragPermutationIndex);

    cache->recordFailure(PrecompiledFallbackReason::PipelineCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PipelineCreated,
                        MakeHitRecord(cache, programInfo, *matchResult));

  ProgramProvenance provenance = {
      ArtifactOriginForBackend(context->backend(), vertexDesc, fragmentDesc),
      ProgramOrigin::PrecompiledArtifact, PipelineOrigin::RuntimeCreation};
  return std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                   std::move(fragmentUniformData), provenance);
}

}  // namespace tgfx
