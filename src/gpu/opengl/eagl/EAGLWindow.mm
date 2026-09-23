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

#include "tgfx/gpu/opengl/eagl/EAGLWindow.h"
#import <Foundation/Foundation.h>
#include "core/utils/Log.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/eagl/EAGLLayerTexture.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
namespace {
class EAGLRenderTargetProxy final : public RenderTargetProxy {
 public:
  EAGLRenderTargetProxy(std::shared_ptr<RenderTargetProxy> proxy,
                        std::shared_ptr<EAGLLayerTexture> layerTexture)
      : proxy(std::move(proxy)), layerTexture(std::move(layerTexture)) {
  }

  Context* getContext() const override {
    return proxy->getContext();
  }

  int width() const override {
    return proxy->width();
  }

  int height() const override {
    return proxy->height();
  }

  PixelFormat format() const override {
    return proxy->format();
  }

  int sampleCount() const override {
    return proxy->sampleCount();
  }

  ImageOrigin origin() const override {
    return proxy->origin();
  }

  bool externallyOwned() const override {
    return proxy->externallyOwned();
  }

  std::shared_ptr<TextureProxy> asTextureProxy() const override {
    return proxy->asTextureProxy();
  }

  std::shared_ptr<TextureView> getTextureView() const override {
    return proxy->getTextureView();
  }

  std::shared_ptr<RenderTarget> getRenderTarget() const override {
    return proxy->getRenderTarget();
  }

  std::shared_ptr<DepthStencilTextureView> getStencil(int sampleCount) override {
    return proxy->getStencil(sampleCount);
  }

  unsigned colorBufferID() const {
    return layerTexture->colorBufferID();
  }

 private:
  std::shared_ptr<RenderTargetProxy> proxy = nullptr;
  std::shared_ptr<EAGLLayerTexture> layerTexture = nullptr;
};
}  // namespace

std::shared_ptr<EAGLWindow> EAGLWindow::MakeFrom(CAEAGLLayer* layer,
                                                 std::shared_ptr<GLDevice> device,
                                                 std::shared_ptr<ColorSpace> colorSpace) {
  if (layer == nil) {
    return nullptr;
  }
  if (device == nullptr) {
    device = GLDevice::MakeWithFallback();
  }
  if (device == nullptr) {
    return nullptr;
  }
  if (colorSpace != nullptr && !ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    LOGE("EAGLWindow::MakeFrom() The specified ColorSpace is not supported on this platform. "
         "Rendering may have color inaccuracies.");
  }
  return std::shared_ptr<EAGLWindow>(new EAGLWindow(device, layer, std::move(colorSpace)));
}

EAGLWindow::EAGLWindow(std::shared_ptr<Device> device, CAEAGLLayer* layer,
                       std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device), std::move(colorSpace)), layer(layer) {
  // do not retain layer here, otherwise it can cause circular reference.
}

std::shared_ptr<RenderTargetProxy> EAGLWindow::onCreateRenderTarget(Context* context) {
  if (![NSThread isMainThread]) {
    LOGE("EAGLWindow::onCreateRenderTarget() must be called on the main thread because it "
         "reads CAEAGLLayer geometry. Create the Surface on the main thread, then render on "
         "any thread.");
  }
  if (!activeRenderTarget.expired()) {
    LOGE("EAGLWindow::onCreateRenderTarget() Only one render target can be active at a time!");
    return nullptr;
  }
  auto gpu = static_cast<GLGPU*>(context->gpu());
  if (layerTexture != nullptr) {
    layerTexture->release(gpu);
    layerTexture = nullptr;
  }
  layerTexture = EAGLLayerTexture::MakeFrom(gpu, layer);
  if (layerTexture == nullptr) {
    return nullptr;
  }
  BackendRenderTarget renderTarget = layerTexture->getBackendRenderTarget();
  auto proxy = RenderTargetProxy::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
  if (proxy == nullptr) {
    layerTexture->release(gpu);
    layerTexture = nullptr;
    return nullptr;
  }
  auto result = std::make_shared<EAGLRenderTargetProxy>(std::move(proxy), layerTexture);
  activeRenderTarget = result;
  return result;
}

void EAGLWindow::onPresent(Context* context,
                           const std::vector<std::shared_ptr<RenderTargetProxy>>& renderTargets) {
  if (renderTargets.empty()) {
    return;
  }
  auto proxy = std::static_pointer_cast<EAGLRenderTargetProxy>(renderTargets.front());
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  gl->bindRenderbuffer(GL_RENDERBUFFER, proxy->colorBufferID());
  auto eaglContext = static_cast<EAGLDevice*>(context->device())->eaglContext();
  [eaglContext presentRenderbuffer:GL_RENDERBUFFER];
  gl->bindRenderbuffer(GL_RENDERBUFFER, 0);
}
}  // namespace tgfx
