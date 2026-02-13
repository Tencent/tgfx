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

#include "MetalRenderPipeline.h"
#include "MetalGPU.h"
#include "MetalShaderModule.h"
#include "MetalDefines.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<MetalRenderPipeline> MetalRenderPipeline::Make(MetalGPU* gpu, 
                                                          const RenderPipelineDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }
  
  auto pipeline = gpu->makeResource<MetalRenderPipeline>(gpu, descriptor);
  if (pipeline->pipelineState == nil) {
    return nullptr;
  }
  return pipeline;
}

MetalRenderPipeline::MetalRenderPipeline(MetalGPU* gpu, const RenderPipelineDescriptor& descriptor) {
  createPipelineState(gpu, descriptor);
  createDepthStencilState(gpu->device(), descriptor);

  // Save cull mode and front face from the primitive descriptor.
  cullMode = MetalDefines::ToMTLCullMode(descriptor.primitive.cullMode);
  frontFace = MetalDefines::ToMTLWinding(descriptor.primitive.frontFace);

  // Build texture binding mapping: logical binding -> Metal texture index (from 0).
  // This decouples the caller's binding numbers from the actual Metal texture slots,
  // matching the approach used by the OpenGL backend (GLRenderPipeline::textureUnits).
  unsigned textureUnit = 0;
  for (auto& entry : descriptor.layout.textureSamplers) {
    textureUnits[entry.binding] = textureUnit++;
  }
}

void MetalRenderPipeline::onRelease(MetalGPU*) {
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

unsigned MetalRenderPipeline::getTextureIndex(unsigned binding) const {
  auto result = textureUnits.find(binding);
  if (result != textureUnits.end()) {
    return result->second;
  }
  return binding;
}

bool MetalRenderPipeline::createPipelineState(MetalGPU* gpu, const RenderPipelineDescriptor& descriptor) {
  auto device = gpu->device();
  // Create Metal render pipeline descriptor
  MTLRenderPipelineDescriptor* metalDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
  
  // Create vertex function from library
  if (descriptor.vertex.module) {
    auto vertexShader = std::static_pointer_cast<MetalShaderModule>(descriptor.vertex.module);
    metalDescriptor.vertexFunction = [vertexShader->metalLibrary() newFunctionWithName:@"main0"];
  }
  
  // Create fragment function from library, with optional sample mask injection
  if (descriptor.fragment.module) {
    auto fragmentShader = std::static_pointer_cast<MetalShaderModule>(descriptor.fragment.module);
    bool needsSampleMask = descriptor.multisample.mask != 0xFFFFFFFF;
    if (needsSampleMask) {
      // Re-compile the fragment shader with sample mask injection using a two-pass approach
      // that picks a constant_id not conflicting with user-defined specialization constants.
      auto compileResult =
          CompileFragmentShaderWithSampleMask(device, fragmentShader->glslCode());
      if (compileResult.library == nil) {
        LOGE("Metal pipeline creation error: sample mask shader compilation failed");
        [metalDescriptor.vertexFunction release];
        [metalDescriptor release];
        return false;
      }
      sampleMaskLibrary = compileResult.library;
      MTLFunctionConstantValues* constants = [[MTLFunctionConstantValues alloc] init];
      uint32_t mask = descriptor.multisample.mask;
      [constants setConstantValue:&mask type:MTLDataTypeUInt atIndex:compileResult.constantID];
      NSError* funcError = nil;
      metalDescriptor.fragmentFunction =
          [sampleMaskLibrary newFunctionWithName:@"main0" constantValues:constants error:&funcError];
      [constants release];
      if (funcError) {
        LOGE("Metal fragment function creation error: %s", funcError.localizedDescription.UTF8String);
      }
    } else {
      metalDescriptor.fragmentFunction =
          [fragmentShader->metalLibrary() newFunctionWithName:@"main0"];
    }
  }
  
  if (metalDescriptor.vertexFunction == nil || metalDescriptor.fragmentFunction == nil) {
    LOGE("Metal pipeline creation error: vertex or fragment function is nil");
    [metalDescriptor.vertexFunction release];
    [metalDescriptor.fragmentFunction release];
    [metalDescriptor release];
    return false;
  }
  
  // Configure vertex descriptor
  if (!descriptor.vertex.bufferLayouts.empty()) {
    DEBUG_ASSERT(descriptor.vertex.bufferLayouts.size() < kVertexBufferIndexStart);
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    
    NSUInteger globalAttributeIndex = 0;
    for (size_t bufferIndex = 0; bufferIndex < descriptor.vertex.bufferLayouts.size(); ++bufferIndex) {
      const auto& layout = descriptor.vertex.bufferLayouts[bufferIndex];
      
      // Vertex buffers use high Metal buffer indices (30, 29, ...) to avoid conflict with
      // uniform buffers at low indices (0, 1, ...).
      auto metalBufferIndex = kVertexBufferIndexStart - bufferIndex;

      // Set buffer layout
      vertexDescriptor.layouts[metalBufferIndex].stride = layout.stride;
      vertexDescriptor.layouts[metalBufferIndex].stepFunction = 
          (layout.stepMode == VertexStepMode::Vertex) ? MTLVertexStepFunctionPerVertex : MTLVertexStepFunctionPerInstance;
      vertexDescriptor.layouts[metalBufferIndex].stepRate = 1;
      
      // Set vertex attributes with globally unique indices across all buffer layouts
      size_t currentOffset = 0;
      for (size_t attrIndex = 0; attrIndex < layout.attributes.size(); ++attrIndex) {
        const auto& attribute = layout.attributes[attrIndex];
        if (globalAttributeIndex < 31) {
          vertexDescriptor.attributes[globalAttributeIndex].format = MetalDefines::ToMTLVertexFormat(attribute.format());
          vertexDescriptor.attributes[globalAttributeIndex].offset = currentOffset;
          vertexDescriptor.attributes[globalAttributeIndex].bufferIndex = metalBufferIndex;
          currentOffset += attribute.size();
          globalAttributeIndex++;
        }
      }
    }
    
    metalDescriptor.vertexDescriptor = vertexDescriptor;
    [vertexDescriptor release];
  }
  
  // Apply multisample settings
  if (descriptor.multisample.count > 1) {
    metalDescriptor.rasterSampleCount = static_cast<NSUInteger>(descriptor.multisample.count);
  }
  metalDescriptor.alphaToCoverageEnabled = descriptor.multisample.alphaToCoverageEnabled;
  
  // Configure color attachments
  for (size_t i = 0; i < descriptor.fragment.colorAttachments.size(); ++i) {
    const auto& colorAttachment = descriptor.fragment.colorAttachments[i];
    
    MTLPixelFormat pixelFormat = gpu->getMTLPixelFormat(colorAttachment.format);
    metalDescriptor.colorAttachments[i].pixelFormat = pixelFormat;
    
    // Configure blending
    if (colorAttachment.blendEnable) {
      metalDescriptor.colorAttachments[i].blendingEnabled = YES;
      metalDescriptor.colorAttachments[i].sourceRGBBlendFactor = 
          MetalDefines::ToMTLBlendFactor(colorAttachment.srcColorBlendFactor);
      metalDescriptor.colorAttachments[i].destinationRGBBlendFactor = 
          MetalDefines::ToMTLBlendFactor(colorAttachment.dstColorBlendFactor);
      metalDescriptor.colorAttachments[i].rgbBlendOperation = 
          MetalDefines::ToMTLBlendOperation(colorAttachment.colorBlendOp);
      metalDescriptor.colorAttachments[i].sourceAlphaBlendFactor = 
          MetalDefines::ToMTLBlendFactor(colorAttachment.srcAlphaBlendFactor);
      metalDescriptor.colorAttachments[i].destinationAlphaBlendFactor = 
          MetalDefines::ToMTLBlendFactor(colorAttachment.dstAlphaBlendFactor);
      metalDescriptor.colorAttachments[i].alphaBlendOperation = 
          MetalDefines::ToMTLBlendOperation(colorAttachment.alphaBlendOp);
    }
    
    // Configure write mask
    MTLColorWriteMask writeMask = MTLColorWriteMaskNone;
    if (colorAttachment.colorWriteMask & ColorWriteMask::RED) writeMask |= MTLColorWriteMaskRed;
    if (colorAttachment.colorWriteMask & ColorWriteMask::GREEN) writeMask |= MTLColorWriteMaskGreen;
    if (colorAttachment.colorWriteMask & ColorWriteMask::BLUE) writeMask |= MTLColorWriteMaskBlue;
    if (colorAttachment.colorWriteMask & ColorWriteMask::ALPHA) writeMask |= MTLColorWriteMaskAlpha;
    metalDescriptor.colorAttachments[i].writeMask = writeMask;
  }
  
  // Create pipeline state
  NSError* error = nil;
  pipelineState = [device newRenderPipelineStateWithDescriptor:metalDescriptor error:&error];
  // Release temporary functions - the pipeline state retains them internally
  [metalDescriptor.vertexFunction release];
  [metalDescriptor.fragmentFunction release];
  [metalDescriptor release];
  
  if (!pipelineState || error) {
    if (error) {
      LOGE("Metal pipeline creation error: %s", error.localizedDescription.UTF8String);
    }
    return false;
  }
  
  return true;
}

bool MetalRenderPipeline::createDepthStencilState(id<MTLDevice> device, const RenderPipelineDescriptor& descriptor) {
  // Always create a depth stencil state for consistency
  MTLDepthStencilDescriptor* depthStencilDescriptor = [[MTLDepthStencilDescriptor alloc] init];
  
  // Configure depth test
  depthStencilDescriptor.depthCompareFunction = 
      MetalDefines::ToMTLCompareFunction(descriptor.depthStencil.depthCompare);
  depthStencilDescriptor.depthWriteEnabled = descriptor.depthStencil.depthWriteEnabled;
  
  // Configure front face stencil
  MTLStencilDescriptor* frontStencil = [[MTLStencilDescriptor alloc] init];
  frontStencil.stencilCompareFunction = 
      MetalDefines::ToMTLCompareFunction(descriptor.depthStencil.stencilFront.compare);
  frontStencil.stencilFailureOperation = 
      MetalDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilFront.failOp);
  frontStencil.depthFailureOperation = 
      MetalDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilFront.depthFailOp);
  frontStencil.depthStencilPassOperation = 
      MetalDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilFront.passOp);
  frontStencil.readMask = descriptor.depthStencil.stencilReadMask;
  frontStencil.writeMask = descriptor.depthStencil.stencilWriteMask;
  
  depthStencilDescriptor.frontFaceStencil = frontStencil;
  [frontStencil release];
  
  // Back face stencil
  MTLStencilDescriptor* backStencil = [[MTLStencilDescriptor alloc] init];
  backStencil.stencilCompareFunction = 
      MetalDefines::ToMTLCompareFunction(descriptor.depthStencil.stencilBack.compare);
  backStencil.stencilFailureOperation = 
      MetalDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilBack.failOp);
  backStencil.depthFailureOperation = 
      MetalDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilBack.depthFailOp);
  backStencil.depthStencilPassOperation = 
      MetalDefines::ToMTLStencilOperation(descriptor.depthStencil.stencilBack.passOp);
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
