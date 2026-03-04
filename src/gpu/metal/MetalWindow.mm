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

#include "tgfx/gpu/metal/MetalWindow.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include "MetalCommandQueue.h"
#include "MetalGPU.h"
#include "core/utils/Log.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/metal/MetalTypes.h"

namespace tgfx {

static NSString* const CopyShaderSource = @R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
  float4 position [[position]];
  float2 texCoord;
};

vertex VertexOut copyVertex(uint vertexID [[vertex_id]]) {
  // Full-screen triangle: 3 vertices covering the entire clip space.
  float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
  float2 texCoords[3] = {float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0)};
  VertexOut out;
  out.position = float4(positions[vertexID], 0.0, 1.0);
  out.texCoord = texCoords[vertexID];
  return out;
}

fragment float4 copyFragment(VertexOut in [[stage_in]],
                             texture2d<float> srcTexture [[texture(0)]]) {
  constexpr sampler texSampler(mag_filter::nearest, min_filter::nearest);
  return srcTexture.sample(texSampler, in.texCoord);
}
)msl";

std::shared_ptr<MetalWindow> MetalWindow::MakeFrom(CAMetalLayer* layer,
                                                   std::shared_ptr<MetalDevice> device,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (layer == nil) {
    return nullptr;
  }
  if (device == nullptr) {
    if (layer.device != nil) {
      device = MetalDevice::MakeFrom((__bridge void*)layer.device);
    } else {
      device = MetalDevice::Make();
    }
  }
  if (device == nullptr) {
    return nullptr;
  }
  if (layer.device == nil) {
    layer.device = (__bridge id<MTLDevice>)device->metalDevice();
  }
  return std::shared_ptr<MetalWindow>(new MetalWindow(device, layer, std::move(colorSpace)));
}

std::shared_ptr<MetalWindow> MetalWindow::MakeFrom(MTKView* view,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (view == nil) {
    return nullptr;
  }
  auto layer = static_cast<CAMetalLayer*>(view.layer);
  if (layer == nil) {
    return nullptr;
  }
  auto device = layer.device != nil ? MetalDevice::MakeFrom((__bridge void*)layer.device)
                                    : MetalDevice::Make();
  if (device == nullptr) {
    return nullptr;
  }
  if (layer.device == nil) {
    layer.device = (__bridge id<MTLDevice>)device->metalDevice();
  }
  return std::shared_ptr<MetalWindow>(new MetalWindow(device, view, layer, std::move(colorSpace)));
}

// Do not retain layer/view here, otherwise it can cause circular reference.
MetalWindow::MetalWindow(std::shared_ptr<Device> device, CAMetalLayer* layer,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)), metalLayer(layer), colorSpace(std::move(colorSpace)) {
}

MetalWindow::MetalWindow(std::shared_ptr<Device> device, MTKView* view, CAMetalLayer* layer,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)),
      metalLayer(layer),
      metalView(view),
      colorSpace(std::move(colorSpace)) {
}

MetalWindow::~MetalWindow() {
  [offscreenTexture release];
  [copyPipelineState release];
}

std::shared_ptr<Surface> MetalWindow::onCreateSurface(Context* context) {
  if (metalView != nil) {
    metalLayer.drawableSize = metalView.drawableSize;
  }
  auto drawableSize = metalLayer.drawableSize;
  auto width = static_cast<int>(drawableSize.width);
  auto height = static_cast<int>(drawableSize.height);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (offscreenTexture == nil || offscreenWidth != width || offscreenHeight != height) {
    [offscreenTexture release];
    auto metalDevice = static_cast<MetalDevice*>(device.get());
    auto mtlDevice = (__bridge id<MTLDevice>)metalDevice->metalDevice();
    auto desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalLayer.pixelFormat
                                                                   width:(NSUInteger)width
                                                                  height:(NSUInteger)height
                                                               mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    offscreenTexture = [mtlDevice newTextureWithDescriptor:desc];
    offscreenWidth = width;
    offscreenHeight = height;
  }
  MetalTextureInfo metalInfo = {};
  metalInfo.texture = (__bridge const void*)offscreenTexture;
  metalInfo.format = static_cast<unsigned>(offscreenTexture.pixelFormat);
  BackendRenderTarget renderTarget(metalInfo, width, height);
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::TopLeft, 0, colorSpace);
}

void MetalWindow::onPresent(Context* context) {
  if (offscreenTexture == nil) {
    return;
  }
  auto drawable = [metalLayer nextDrawable];
  if (drawable == nil) {
    return;
  }

  auto metalGPU = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(metalGPU->queue());
  auto commandBuffer = [queue->metalCommandQueue() commandBuffer];

  if (metalLayer.framebufferOnly) {
    renderCopyToDrawable(commandBuffer, drawable);
  } else {
    blitToDrawable(commandBuffer, drawable);
  }

  [commandBuffer presentDrawable:drawable];
  [commandBuffer commit];
}

void MetalWindow::blitToDrawable(id<MTLCommandBuffer> commandBuffer, id<CAMetalDrawable> drawable) {
  auto blitEncoder = [commandBuffer blitCommandEncoder];
  [blitEncoder copyFromTexture:offscreenTexture toTexture:drawable.texture];
  [blitEncoder endEncoding];
}

void MetalWindow::renderCopyToDrawable(id<MTLCommandBuffer> commandBuffer,
                                       id<CAMetalDrawable> drawable) {
  auto metalDevice = static_cast<MetalDevice*>(device.get());
  auto mtlDevice = (__bridge id<MTLDevice>)metalDevice->metalDevice();
  ensureCopyPipelineState(mtlDevice, drawable.texture.pixelFormat);
  if (copyPipelineState == nil) {
    return;
  }

  auto renderPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];
  renderPassDesc.colorAttachments[0].texture = drawable.texture;
  renderPassDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
  renderPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

  auto renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDesc];
  [renderEncoder setRenderPipelineState:copyPipelineState];
  [renderEncoder setFragmentTexture:offscreenTexture atIndex:0];
  [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  [renderEncoder endEncoding];
}

void MetalWindow::ensureCopyPipelineState(id<MTLDevice> device, MTLPixelFormat pixelFormat) {
  if (copyPipelineState != nil && copyPixelFormat == pixelFormat) {
    return;
  }
  [copyPipelineState release];
  copyPipelineState = nil;
  copyPixelFormat = MTLPixelFormatInvalid;

  NSError* error = nil;
  auto library = [device newLibraryWithSource:CopyShaderSource options:nil error:&error];
  if (library == nil) {
    LOGE("MetalWindow::ensureCopyPipelineState() failed to compile copy shader: %s",
         error.localizedDescription.UTF8String);
    return;
  }
  auto vertexFunc = [library newFunctionWithName:@"copyVertex"];
  auto fragmentFunc = [library newFunctionWithName:@"copyFragment"];

  auto pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
  pipelineDesc.vertexFunction = vertexFunc;
  pipelineDesc.fragmentFunction = fragmentFunc;
  pipelineDesc.colorAttachments[0].pixelFormat = pixelFormat;

  copyPipelineState = [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
  if (copyPipelineState != nil) {
    copyPixelFormat = pixelFormat;
  } else {
    LOGE("MetalWindow::ensureCopyPipelineState() failed to create pipeline state: %s",
         error.localizedDescription.UTF8String);
  }

  [pipelineDesc release];
  [fragmentFunc release];
  [vertexFunc release];
  [library release];
}

void MetalWindow::onFreeSurface() {
  Window::onFreeSurface();
  [offscreenTexture release];
  offscreenTexture = nil;
  offscreenWidth = 0;
  offscreenHeight = 0;
}

}  // namespace tgfx
