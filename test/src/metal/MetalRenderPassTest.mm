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

#include <Metal/Metal.h>
#include "gpu/metal/MetalGPU.h"
#include "gpu/metal/MetalRenderPass.h"
#include "gpu/metal/MetalRenderPipeline.h"
#include "gpu/metal/MetalShaderModule.h"
#include "gpu/metal/MetalBuffer.h"
#include "gpu/metal/MetalTexture.h"
#include "tgfx/gpu/CommandEncoder.h"
#include "core/utils/Log.h"
#include "utils/TestUtils.h"

namespace tgfx {

// Simple MSL shader that renders a solid color triangle
static const char* vertexShaderMSL = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
};

struct VertexOut {
    float4 position [[position]];
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    return out;
}
)";

static const char* fragmentShaderMSL = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
};

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return float4(1.0, 0.0, 0.0, 1.0);  // Red color
}
)";

// Helper class to create MetalShaderModule directly with MSL code
class TestMetalShaderModule : public ShaderModule {
 public:
  static std::shared_ptr<TestMetalShaderModule> Make(MetalGPU* gpu, const std::string& mslCode,
                                                   bool isVertex) {
    if (!gpu) {
      return nullptr;
    }

    NSString* source = [NSString stringWithUTF8String:mslCode.c_str()];
    NSError* error = nil;
    id<MTLLibrary> library = [gpu->device() newLibraryWithSource:source options:nil error:&error];

    if (!library) {
      if (error) {
        LOGE("Metal shader compilation error: %s", error.localizedDescription.UTF8String);
      }
      return nullptr;
    }

    id<MTLFunction> function = nil;
    if (isVertex) {
      function = [library newFunctionWithName:@"vertex_main"];
    } else {
      function = [library newFunctionWithName:@"fragment_main"];
    }

    if (!function) {
      LOGE("Failed to find shader function");
      [library release];
      return nullptr;
    }

    return std::make_shared<TestMetalShaderModule>(library, function, isVertex);
  }

  id<MTLLibrary> metalLibrary() const {
    return library;
  }

  id<MTLFunction> vertexFunction() const {
    return isVertexShader ? function : nil;
  }

  id<MTLFunction> fragmentFunction() const {
    return isVertexShader ? nil : function;
  }

  TestMetalShaderModule(id<MTLLibrary> lib, id<MTLFunction> func, bool isVertex)
      : library(lib), function(func), isVertexShader(isVertex) {
  }

  ~TestMetalShaderModule() override {
    if (function != nil) {
      [function release];
    }
    if (library != nil) {
      [library release];
    }
  }

 private:
  id<MTLLibrary> library = nil;
  id<MTLFunction> function = nil;
  bool isVertexShader = true;
};

// Helper to create render pipeline directly with Metal
static id<MTLRenderPipelineState> CreateTestPipeline(MetalGPU* gpu,
                                                     std::shared_ptr<TestMetalShaderModule> vertexModule,
                                                     std::shared_ptr<TestMetalShaderModule> fragmentModule,
                                                     MTLPixelFormat pixelFormat) {
  MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction = vertexModule->vertexFunction();
  descriptor.fragmentFunction = fragmentModule->fragmentFunction();
  descriptor.colorAttachments[0].pixelFormat = pixelFormat;

  // Configure vertex descriptor
  MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
  vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
  vertexDescriptor.attributes[0].offset = 0;
  vertexDescriptor.attributes[0].bufferIndex = 0;
  vertexDescriptor.layouts[0].stride = sizeof(float) * 2;
  vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
  descriptor.vertexDescriptor = vertexDescriptor;
  [vertexDescriptor release];

  NSError* error = nil;
  id<MTLRenderPipelineState> pipelineState =
      [gpu->device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
  [descriptor release];

  if (!pipelineState) {
    if (error) {
      LOGE("Pipeline creation error: %s", error.localizedDescription.UTF8String);
    }
    return nil;
  }

  return pipelineState;
}

TGFX_TEST(MetalRenderPassTest, BasicTriangle) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }

  auto gpu = static_cast<MetalGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  // Create shaders
  auto vertexModule = TestMetalShaderModule::Make(gpu, vertexShaderMSL, true);
  ASSERT_TRUE(vertexModule != nullptr);

  auto fragmentModule = TestMetalShaderModule::Make(gpu, fragmentShaderMSL, false);
  ASSERT_TRUE(fragmentModule != nullptr);

  // Create render target texture
  int width = 200;
  int height = 200;
  TextureDescriptor textureDesc = {};
  textureDesc.width = width;
  textureDesc.height = height;
  textureDesc.format = PixelFormat::RGBA_8888;
  textureDesc.usage = TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING;

  auto renderTarget = gpu->createTexture(textureDesc);
  ASSERT_TRUE(renderTarget != nullptr);

  // Create vertex buffer with a triangle
  // NDC coordinates: top-center, bottom-left, bottom-right
  float vertices[] = {
      0.0f,  0.5f,   // top center
      -0.5f, -0.5f,  // bottom left
      0.5f,  -0.5f   // bottom right
  };

  auto vertexBuffer = gpu->createBuffer(sizeof(vertices), GPUBufferUsage::VERTEX);
  ASSERT_TRUE(vertexBuffer != nullptr);

  void* bufferData = vertexBuffer->map();
  ASSERT_TRUE(bufferData != nullptr);
  memcpy(bufferData, vertices, sizeof(vertices));
  vertexBuffer->unmap();

  // Create pipeline
  auto metalTexture = std::static_pointer_cast<MetalTexture>(renderTarget);
  auto pipelineState =
      CreateTestPipeline(gpu, vertexModule, fragmentModule, metalTexture->metalTexture().pixelFormat);
  ASSERT_TRUE(pipelineState != nil);

  // Create command encoder and render pass
  auto commandEncoder = gpu->createCommandEncoder();
  ASSERT_TRUE(commandEncoder != nullptr);

  RenderPassDescriptor renderPassDesc(renderTarget, LoadAction::Clear, StoreAction::Store,
                                      Color::White().premultiply());

  auto renderPass = commandEncoder->beginRenderPass(renderPassDesc);
  ASSERT_TRUE(renderPass != nullptr);

  // Get the Metal render pass to set pipeline and draw
  auto metalRenderPass = std::static_pointer_cast<MetalRenderPass>(renderPass);
  auto encoder = metalRenderPass->metalRenderCommandEncoder();
  ASSERT_TRUE(encoder != nil);

  // Set viewport
  MTLViewport viewport = {};
  viewport.originX = 0;
  viewport.originY = 0;
  viewport.width = width;
  viewport.height = height;
  viewport.znear = 0.0;
  viewport.zfar = 1.0;
  [encoder setViewport:viewport];

  // Set pipeline and draw
  [encoder setRenderPipelineState:pipelineState];

  auto metalBuffer = std::static_pointer_cast<MetalBuffer>(vertexBuffer);
  [encoder setVertexBuffer:metalBuffer->metalBuffer() offset:0 atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

  renderPass->end();

  // Finish and submit
  auto commandBuffer = commandEncoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);

  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();
  [pipelineState release];

  // For now, just verify the test runs without crash
  // Full pixel comparison would require readback implementation
  SUCCEED();
}

TGFX_TEST(MetalRenderPassTest, ClearOnly) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }

  auto gpu = static_cast<MetalGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  // Create render target texture
  int width = 100;
  int height = 100;
  TextureDescriptor textureDesc = {};
  textureDesc.width = width;
  textureDesc.height = height;
  textureDesc.format = PixelFormat::RGBA_8888;
  textureDesc.usage = TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING;

  auto renderTarget = gpu->createTexture(textureDesc);
  ASSERT_TRUE(renderTarget != nullptr);

  // Create command encoder
  auto commandEncoder = gpu->createCommandEncoder();
  ASSERT_TRUE(commandEncoder != nullptr);

  // Create render pass that just clears to red
  RenderPassDescriptor renderPassDesc(renderTarget, LoadAction::Clear, StoreAction::Store,
                                      Color::Red().premultiply());

  auto renderPass = commandEncoder->beginRenderPass(renderPassDesc);
  ASSERT_TRUE(renderPass != nullptr);

  // Just end the pass (clear only)
  renderPass->end();

  // Finish and submit
  auto commandBuffer = commandEncoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);

  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  SUCCEED();
}

}  // namespace tgfx
