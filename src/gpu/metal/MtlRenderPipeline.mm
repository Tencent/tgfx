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

#include "MtlRenderPipeline.h"
#include "MtlGPU.h"
#include "MtlShaderModule.h"
#include "MtlDefines.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<MtlRenderPipeline> MtlRenderPipeline::Make(MtlGPU* gpu, 
                                                          const RenderPipelineDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }
  
  auto pipeline = gpu->makeResource<MtlRenderPipeline>(gpu, descriptor);
  if (pipeline->pipelineState == nil) {
    return nullptr;
  }
  return pipeline;
}

MtlRenderPipeline::MtlRenderPipeline(MtlGPU* gpu, const RenderPipelineDescriptor& descriptor) {
  createPipelineState(gpu, descriptor);
  createDepthStencilState(gpu->device(), descriptor);

  // Save cull mode and front face from the primitive descriptor.
  cullMode = MtlDefines::ToMTLCullMode(descriptor.primitive.cullMode);
  frontFace = MtlDefines::ToMTLWinding(descriptor.primitive.frontFace);

  // Build texture binding mapping: logical binding -> Metal texture index (from 0).
  // This decouples the caller's binding numbers from the actual Metal texture slots,
  // matching the approach used by the OpenGL backend (GLRenderPipeline::textureUnits).
  unsigned textureUnit = 0;
  for (auto& entry : descriptor.layout.textureSamplers) {
    textureUnits[entry.binding] = textureUnit++;
  }
}

void MtlRenderPipeline::onRelease(MtlGPU*) {
  if (sampleMaskLibrary != nil) {
    [sampleMaskLibrary release];
    sampleMaskLibrary = nil;
  }
  if (depthStencilState != nil) {
    [depthStencilState release];
    depthStencilState = nil;
  }
  if (pipelineState != nil) {
    [pipelineState release];
    pipelineState = nil;
  }
}

unsigned MtlRenderPipeline::getTextureIndex(unsigned binding) const {
  auto result = textureUnits.find(binding);
  if (result != textureUnits.end()) {
    return result->second;
  }
  return binding;
}

bool MtlRenderPipeline::createPipelineState(MtlGPU* gpu, const RenderPipelineDescriptor& descriptor) {
  auto device = gpu->device();
  // Create Metal render pipeline descriptor
  MTLRenderPipelineDescriptor* mtlDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
  
  // Create vertex function from library
  if (descriptor.vertex.module) {
    auto vertexShader = std::static_pointer_cast<MtlShaderModule>(descriptor.vertex.module);
    mtlDescriptor.vertexFunction = [vertexShader->mtlLibrary() newFunctionWithName:@"main0"];
  }
  
  // Create fragment function from library, with optional sample mask injection
  if (descriptor.fragment.module) {
    auto fragmentShader = std::static_pointer_cast<MtlShaderModule>(descriptor.fragment.module);
    bool needsSampleMask = descriptor.multisample.mask != 0xFFFFFFFF;
    if (needsSampleMask) {
      // Re-compile the fragment shader with sample mask injection using a two-pass approach
      // that picks a constant_id not conflicting with user-defined specialization constants.
      auto compileResult =
          CompileFragmentShaderWithSampleMask(device, fragmentShader->glslCode());
      if (compileResult.library == nil) {
        LOGE("Metal pipeline creation error: sample mask shader compilation failed");
        [mtlDescriptor.vertexFunction release];
        [mtlDescriptor release];
        return false;
      }
      sampleMaskLibrary = compileResult.library;
      MTLFunctionConstantValues* constants = [[MTLFunctionConstantValues alloc] init];
      uint32_t mask = descriptor.multisample.mask;
      [constants setConstantValue:&mask type:MTLDataTypeUInt atIndex:compileResult.constantID];
      NSError* funcError = nil;
      mtlDescriptor.fragmentFunction =
          [sampleMaskLibrary newFunctionWithName:@"main0" constantValues:constants error:&funcError];
      [constants release];
      if (funcError) {
        LOGE("Metal fragment function creation error: %s", funcError.localizedDescription.UTF8String);
      }
    } else {
      mtlDescriptor.fragmentFunction =
          [fragmentShader->mtlLibrary() newFunctionWithName:@"main0"];
    }
  }
  
  if (mtlDescriptor.vertexFunction == nil || mtlDescriptor.fragmentFunction == nil) {
    LOGE("Metal pipeline creation error: vertex or fragment function is nil");
    [mtlDescriptor.vertexFunction release];
    [mtlDescriptor.fragmentFunction release];
    [mtlDescriptor release];
    return false;
  }
  
  // Configure vertex descriptor
  if (!descriptor.vertex.bufferLayouts.empty()) {
    DEBUG_ASSERT(descriptor.vertex.bufferLayouts.size() <= kVertexBufferIndexStart + 1);
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    
    NSUInteger globalAttributeIndex = 0;
    for (size_t bufferIndex = 0; bufferIndex < descriptor.vertex.bufferLayouts.size(); ++bufferIndex) {
      const auto& layout = descriptor.vertex.bufferLayouts[bufferIndex];
      
      // Vertex buffers use high Metal buffer indices (30, 29, ...) to avoid conflict with
      // uniform buffers at low indices (0, 1, ...).
      auto mtlBufferIndex = kVertexBufferIndexStart - bufferIndex;

      // Set buffer layout
      vertexDescriptor.layouts[mtlBufferIndex].stride = layout.stride;
      vertexDescriptor.layouts[mtlBufferIndex].stepFunction = 
          (layout.stepMode == VertexStepMode::Vertex) ? MTLVertexStepFunctionPerVertex : MTLVertexStepFunctionPerInstance;
      vertexDescriptor.layouts[mtlBufferIndex].stepRate = 1;
      
      // Set vertex attributes with globally unique indices across all buffer layouts
      size_t currentOffset = 0;
      for (size_t attrIndex = 0; attrIndex < layout.attributes.size(); ++attrIndex) {
        const auto& attribute = layout.attributes[attrIndex];
        if (globalAttributeIndex < 31) {
          vertexDescriptor.attributes[globalAttributeIndex].format = MtlDefines::ToMTLVertexFormat(attribute.format());
          vertexDescriptor.attributes[globalAttributeIndex].offset = currentOffset;
          vertexDescriptor.attributes[globalAttributeIndex].bufferIndex = mtlBufferIndex;
          currentOffset += attribute.size();
          globalAttributeIndex++;
        }
      }
    }
    
    mtlDescriptor.vertexDescriptor = vertexDescriptor;
    [vertexDescriptor release];
  }
  
  // Apply multisample settings
  if (descriptor.multisample.count > 1) {
    mtlDescriptor.rasterSampleCount = static_cast<NSUInteger>(descriptor.multisample.count);
  }
  mtlDescriptor.alphaToCoverageEnabled = descriptor.multisample.alphaToCoverageEnabled;
  
  // Configure color attachments
  for (size_t i = 0; i < descriptor.fragment.colorAttachments.size(); ++i) {
    const auto& colorAttachment = descriptor.fragment.colorAttachments[i];
    
    MTLPixelFormat pixelFormat = gpu->getMTLPixelFormat(colorAttachment.format);
    mtlDescriptor.colorAttachments[i].pixelFormat = pixelFormat;
    
    // Configure blending
    if (colorAttachment.blendEnable) {
      mtlDescriptor.colorAttachments[i].blendingEnabled = YES;
      mtlDescriptor.colorAttachments[i].sourceRGBBlendFactor = 
          MtlDefines::ToMTLBlendFactor(colorAttachment.srcColorBlendFactor);
      mtlDescriptor.colorAttachments[i].destinationRGBBlendFactor = 
          MtlDefines::ToMTLBlendFactor(colorAttachment.dstColorBlendFactor);
      mtlDescriptor.colorAttachments[i].rgbBlendOperation = 
          MtlDefines::ToMTLBlendOperation(colorAttachment.colorBlendOp);
      mtlDescriptor.colorAttachments[i].sourceAlphaBlendFactor = 
          MtlDefines::ToMTLBlendFactor(colorAttachment.srcAlphaBlendFactor);
      mtlDescriptor.colorAttachments[i].destinationAlphaBlendFactor = 
          MtlDefines::ToMTLBlendFactor(colorAttachment.dstAlphaBlendFactor);
      mtlDescriptor.colorAttachments[i].alphaBlendOperation = 
          MtlDefines::ToMTLBlendOperation(colorAttachment.alphaBlendOp);
    }
    
    // Configure write mask
    MTLColorWriteMask writeMask = MTLColorWriteMaskNone;
    if (colorAttachment.colorWriteMask & ColorWriteMask::RED) writeMask |= MTLColorWriteMaskRed;
    if (colorAttachment.colorWriteMask & ColorWriteMask::GREEN) writeMask |= MTLColorWriteMaskGreen;
    if (colorAttachment.colorWriteMask & ColorWriteMask::BLUE) writeMask |= MTLColorWriteMaskBlue;
    if (colorAttachment.colorWriteMask & ColorWriteMask::ALPHA) writeMask |= MTLColorWriteMaskAlpha;
    mtlDescriptor.colorAttachments[i].writeMask = writeMask;
  }
  
  // Create pipeline state
  NSError* error = nil;
  pipelineState = [device newRenderPipelineStateWithDescriptor:mtlDescriptor error:&error];
  // Release temporary functions - the pipeline state retains them internally
  [mtlDescriptor.vertexFunction release];
  [mtlDescriptor.fragmentFunction release];
  [mtlDescriptor release];
  
  if (!pipelineState || error) {
    if (error) {
      LOGE("Metal pipeline creation error: %s", error.localizedDescription.UTF8String);
    }
    return false;
  }
  
  return true;
}

bool MtlRenderPipeline::createDepthStencilState(id<MTLDevice> device, const RenderPipelineDescriptor& descriptor) {
  // Always create a depth stencil state for consistency
  MTLDepthStencilDescriptor* depthStencilDescriptor = [[MTLDepthStencilDescriptor alloc] init];
  
  // Configure depth test
  depthStencilDescriptor.depthCompareFunction = 
      MtlDefines::ToMTLCompareFunction(descriptor.depthStencil.depthCompare);
  depthStencilDescriptor.depthWriteEnabled = descriptor.depthStencil.depthWriteEnabled;
  
  // Configure front face stencil
  MTLStencilDescriptor* frontStencil = [[MTLStencilDescriptor alloc] init];
  frontStencil.stencilCompareFunction = 
      MtlDefines::ToMTLCompareFunction(descriptor.depthStencil.stencilFront.compare);
  frontStencil.stencilFailureOperation = 
      MtlDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilFront.failOp);
  frontStencil.depthFailureOperation = 
      MtlDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilFront.depthFailOp);
  frontStencil.depthStencilPassOperation = 
      MtlDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilFront.passOp);
  frontStencil.readMask = descriptor.depthStencil.stencilReadMask;
  frontStencil.writeMask = descriptor.depthStencil.stencilWriteMask;
  
  depthStencilDescriptor.frontFaceStencil = frontStencil;
  [frontStencil release];
  
  // Back face stencil
  MTLStencilDescriptor* backStencil = [[MTLStencilDescriptor alloc] init];
  backStencil.stencilCompareFunction = 
      MtlDefines::ToMTLCompareFunction(descriptor.depthStencil.stencilBack.compare);
  backStencil.stencilFailureOperation = 
      MtlDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilBack.failOp);
  backStencil.depthFailureOperation = 
      MtlDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilBack.depthFailOp);
  backStencil.depthStencilPassOperation = 
      MtlDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilBack.passOp);
  backStencil.readMask = descriptor.depthStencil.stencilReadMask;
  backStencil.writeMask = descriptor.depthStencil.stencilWriteMask;
  
  depthStencilDescriptor.backFaceStencil = backStencil;
  [backStencil release];
  
  // Create depth stencil state
  depthStencilState = [device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
  [depthStencilDescriptor release];
  
  return depthStencilState != nil;
}

}  // namespace tgfx
