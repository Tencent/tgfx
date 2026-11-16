/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/PrecompiledDiagnostics.h"
#include <iomanip>
#include <sstream>
#include "core/utils/Log.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/ir/XferFacts.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/shaders/Families.h"

namespace tgfx {

namespace {

void AppendFragmentProcessor(std::stringstream& stream, const FragmentProcessor* processor) {
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

void AppendStencilSignature(std::stringstream& stream, const char* name,
                            const StencilDescriptor& stencil) {
  stream << ";" << name << "Compare=" << static_cast<uint32_t>(stencil.compare) << ";" << name
         << "Fail=" << static_cast<uint32_t>(stencil.failOp) << ";" << name
         << "DepthFail=" << static_cast<uint32_t>(stencil.depthFailOp) << ";" << name
         << "Pass=" << static_cast<uint32_t>(stencil.passOp);
}

std::string BuildPipelineSignature(const ProgramInfo* programInfo) {
  std::stringstream stream;
  const auto colorAttachment = programInfo->getPipelineColorAttachment();
  const auto& depthStencil = programInfo->getDepthStencil();
  stream << programInfo->getEffectSignature()
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
         << ";DepthWrite=" << static_cast<uint32_t>(depthStencil.depthWriteEnabled ? 1 : 0)
         << ";StencilReadMask=" << depthStencil.stencilReadMask
         << ";StencilWriteMask=" << depthStencil.stencilWriteMask;
  AppendStencilSignature(stream, "StencilFront", depthStencil.stencilFront);
  AppendStencilSignature(stream, "StencilBack", depthStencil.stencilBack);
  return stream.str();
}

}  // namespace

std::string BuildEffectSignature(const GeometryLayout& geometry,
                                 const std::vector<const FragmentProcessor*>& colorProcessors,
                                 const std::vector<const FragmentProcessor*>& coverageProcessors,
                                 const XferFacts& xfer) {
  std::stringstream stream;
  stream << "GP=" << GeometryKindName(geometry.parameters.kind) << ";ColorFP=[";
  for (size_t i = 0; i < colorProcessors.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, colorProcessors[i]);
  }
  stream << "];CoverageFP=[";
  for (size_t i = 0; i < coverageProcessors.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, coverageProcessors[i]);
  }
  stream << "];XP=" << (xfer.shaderBlend() ? "shader" : "fixed");
  return stream.str();
}

PrecompiledFallbackRecord MakeFallbackRecord(const PrecompiledShaderCache* cache,
                                             const ProgramInfo* programInfo,
                                             const Variant* variant) {
  PrecompiledFallbackRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.programKey = ProgramInfo::FormatDiagnosticKey(programInfo->programKey());
  record.effectSignature = programInfo->getEffectSignature();
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  if (variant != nullptr) {
    record.shaderName = Families()[variant->family].name;
    record.variantId = variant->id();
  }
  return record;
}

PrecompiledHitRecord MakeHitRecord(const PrecompiledShaderCache* cache,
                                   const ProgramInfo* programInfo, const Variant& variant) {
  PrecompiledHitRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.programKey = ProgramInfo::FormatDiagnosticKey(programInfo->programKey());
  record.effectSignature = programInfo->getEffectSignature();
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  record.shaderName = Families()[variant.family].name;
  record.variantId = variant.id();
  return record;
}

void LogPipelineCreationFailure(const RenderPipelineDescriptor& descriptor, const char* shaderName,
                                uint32_t variantId) {
  std::ostringstream diag;
  diag << "PrecompiledProgramCreator: Failed to create render pipeline for " << shaderName
       << "[variant=" << variantId << "]";
  if (!descriptor.fragment.colorAttachments.empty()) {
    const auto& color = descriptor.fragment.colorAttachments[0];
    diag << " colorFormat=" << static_cast<int>(color.format)
         << " blendEnable=" << (color.blendEnable ? 1 : 0) << " blend=("
         << static_cast<int>(color.srcColorBlendFactor) << ","
         << static_cast<int>(color.dstColorBlendFactor) << ",op"
         << static_cast<int>(color.colorBlendOp) << " / "
         << static_cast<int>(color.srcAlphaBlendFactor) << ","
         << static_cast<int>(color.dstAlphaBlendFactor) << ",op"
         << static_cast<int>(color.alphaBlendOp) << ") writeMask=0x" << std::hex
         << color.colorWriteMask << std::dec;
  }
  diag << " sampleCount=" << descriptor.multisample.count
       << " depthStencilFormat=" << static_cast<int>(descriptor.depthStencil.format)
       << " depthCompare=" << static_cast<int>(descriptor.depthStencil.depthCompare)
       << " depthWrite=" << (descriptor.depthStencil.depthWriteEnabled ? 1 : 0)
       << " uniformBlocks=" << descriptor.layout.uniformBlocks.size()
       << " samplers=" << descriptor.layout.textureSamplers.size()
       << " vertexBuffers=" << descriptor.vertex.bufferLayouts.size();
  for (size_t i = 0; i < descriptor.vertex.bufferLayouts.size(); ++i) {
    const auto& bufferLayout = descriptor.vertex.bufferLayouts[i];
    diag << " buffer" << i << "(step=" << static_cast<int>(bufferLayout.stepMode)
         << ",stride=" << bufferLayout.stride << ",attrs=";
    for (const auto& attribute : bufferLayout.attributes) {
      diag << attribute.name() << ":" << static_cast<int>(attribute.format()) << " ";
    }
    diag << ")";
  }
  LOGE("%s", diag.str().c_str());
}

}  // namespace tgfx
