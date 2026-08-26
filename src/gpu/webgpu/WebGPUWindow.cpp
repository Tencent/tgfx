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

#include "tgfx/gpu/webgpu/WebGPUWindow.h"
#include <webgpu/webgpu.h>
#include <cstdint>
#include "WebGPUDefines.h"
#include "WebGPUDrawableProxy.h"
#include "WebGPUGPU.h"
#include "core/utils/Log.h"
#include "platform/web/WebNamedColorSpace.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <emscripten/val.h>
#endif

namespace tgfx {

// Maps a WGPUTextureFormat to the WebGPU GPUTextureFormat string used by GPUCanvasContext.configure().
static const char* ToWebGPUFormatString(WGPUTextureFormat format) {
  switch (format) {
    case WGPUTextureFormat_RGBA8Unorm:
      return "rgba8unorm";
    case WGPUTextureFormat_BGRA8Unorm:
      return "bgra8unorm";
    case WGPUTextureFormat_RGBA16Float:
      return "rgba16float";
    case WGPUTextureFormat_RGBA32Float:
      return "rgba32float";
    default:
      LOGE("WebGPUWindow: Unsupported surface format (%d), falling back to bgra8unorm.",
           static_cast<int>(format));
      return "bgra8unorm";
  }
}

// Maps a WGPUCompositeAlphaMode to the WebGPU alphaMode string used by GPUCanvasContext.configure().
static const char* ToAlphaModeString(WGPUCompositeAlphaMode alphaMode) {
  switch (alphaMode) {
    case WGPUCompositeAlphaMode_Opaque:
      return "opaque";
    case WGPUCompositeAlphaMode_Unpremultiplied:
      return "unpremultiplied";
    default:
      return "premultiplied";
  }
}

std::shared_ptr<WebGPUWindow> WebGPUWindow::MakeFrom(const std::string& canvasSelector,
                                                     std::shared_ptr<WebGPUDevice> device,
                                                     std::shared_ptr<ColorSpace> colorSpace) {
  if (canvasSelector.empty()) {
    return nullptr;
  }
  if (device == nullptr) {
    device = WebGPUDevice::Make();
  }
  if (device == nullptr) {
    return nullptr;
  }

  WGPUSurface surface = nullptr;
#ifdef __EMSCRIPTEN__
  WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
  canvasDesc.selector = canvasSelector.c_str();

  WGPUSurfaceDescriptor surfaceDesc = {};
  surfaceDesc.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&canvasDesc);

  auto wgpuInstance = wgpuCreateInstance(nullptr);
  surface = wgpuInstanceCreateSurface(wgpuInstance, &surfaceDesc);
  if (wgpuInstance != nullptr) {
    wgpuInstanceRelease(wgpuInstance);
  }
#endif

  if (surface == nullptr) {
    return nullptr;
  }

  auto wgpuDevice = static_cast<WGPUDevice>(device->webgpuDevice());

  // Configure the surface.
  int canvasWidth = 0;
  int canvasHeight = 0;
#ifdef __EMSCRIPTEN__
  emscripten_get_canvas_element_size(canvasSelector.c_str(), &canvasWidth, &canvasHeight);
#endif
  if (canvasWidth <= 0 || canvasHeight <= 0) {
    wgpuSurfaceRelease(surface);
    return nullptr;
  }

  WGPUSurfaceConfiguration config = {};
  config.device = wgpuDevice;
  config.format = WGPUTextureFormat_BGRA8Unorm;
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.width = static_cast<uint32_t>(canvasWidth);
  config.height = static_cast<uint32_t>(canvasHeight);
  // Browser canvas presentation only supports Fifo; Immediate (vsync off) is a native-only
  // extension. Requesting an unsupported present mode makes wgpuSurfaceConfigure raise a validation
  // error and leaves the surface invalid, so always use Fifo here and treat vsyncEnabled=false as a
  // no-op on this backend (matching the "backends that cannot control vsync ignore this setting"
  // contract).
  config.presentMode = WGPUPresentMode_Fifo;
  config.alphaMode = WGPUCompositeAlphaMode_Premultiplied;
  wgpuSurfaceConfigure(surface, &config);

  auto window = std::shared_ptr<WebGPUWindow>(
      new WebGPUWindow(std::move(device), surface, canvasWidth, canvasHeight, canvasSelector,
                       std::move(colorSpace)));
  window->configureColorSpace(config.format, config.usage, config.alphaMode);
  return window;
}

WebGPUWindow::WebGPUWindow(std::shared_ptr<Device> device, void* surface, int width, int height,
                           const std::string& canvasSelector,
                           std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device), std::move(colorSpace)), _canvasSelector(canvasSelector),
      _surface(surface), _width(width), _height(height), _configuredWidth(width),
      _configuredHeight(height) {
}

void WebGPUWindow::configureColorSpace(WGPUTextureFormat format, WGPUTextureUsageFlags usage,
                                       WGPUCompositeAlphaMode alphaMode) {
#ifdef __EMSCRIPTEN__
  auto namedColorSpace = ToWebNamedColorSpace(colorSpace());
  // Leave the default sRGB drawing buffer untouched to avoid a redundant reconfigure on the
  // default rendering path.
  if (namedColorSpace == WebNamedColorSpace::None) {
    return;
  }
  // Reconfigure the canvas WebGPU context with the desired color space via the JS side, since the
  // WebGPU C API surface configuration does not expose a color space option. The JS side resolves
  // the device from the handle below through the module's WebGPU runtime export, which also covers
  // devices passed in via WebGPUDevice::MakeFrom(). The format, usage and alpha mode are passed
  // along so that this WGPUSurfaceConfiguration stays the single source of truth for the canvas.
  auto wgpuDevice =
      static_cast<WGPUDevice>(static_cast<WebGPUDevice*>(getDevice().get())->webgpuDevice());
  bool supported = emscripten::val::module_property("tgfx").call<bool>(
      "configureWebGPUColorSpace", emscripten::val(_canvasSelector),
      static_cast<int>(namedColorSpace), reinterpret_cast<uintptr_t>(wgpuDevice),
      emscripten::val(ToWebGPUFormatString(format)), static_cast<unsigned>(usage),
      emscripten::val(ToAlphaModeString(alphaMode)));
  if (!supported) {
    LOGE(
        "WebGPUWindow::configureColorSpace() The specified ColorSpace is not supported on this "
        "platform. Rendering may have color inaccuracies.");
  }
#endif
}

WebGPUWindow::~WebGPUWindow() {
  if (_surface) {
    wgpuSurfaceRelease(static_cast<WGPUSurface>(_surface));
  }
}

std::shared_ptr<RenderTargetProxy> WebGPUWindow::onCreateRenderTarget(Context* context) {
  if (_surface == nullptr) {
    return nullptr;
  }

  // Re-query canvas size in case it changed (e.g., after updateSize sets canvas.width/height).
#ifdef __EMSCRIPTEN__
  int canvasWidth = 0;
  int canvasHeight = 0;
  emscripten_get_canvas_element_size(_canvasSelector.c_str(), &canvasWidth, &canvasHeight);
  if (canvasWidth > 0 && canvasHeight > 0) {
    _width = canvasWidth;
    _height = canvasHeight;
  }
#endif

  if (_width <= 0 || _height <= 0) {
    return nullptr;
  }

  // Reconfigure the surface when dimensions change. Present mode stays Fifo (see MakeFrom).
  if (_width != _configuredWidth || _height != _configuredHeight) {
    auto wgpuDevice =
        static_cast<WGPUDevice>(static_cast<WebGPUDevice*>(getDevice().get())->webgpuDevice());
    auto wgpuSurface = static_cast<WGPUSurface>(_surface);
    WGPUSurfaceConfiguration config = {};
    config.device = wgpuDevice;
    config.format = WGPUTextureFormat_BGRA8Unorm;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = static_cast<uint32_t>(_width);
    config.height = static_cast<uint32_t>(_height);
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Premultiplied;
    wgpuSurfaceConfigure(wgpuSurface, &config);
    configureColorSpace(config.format, config.usage, config.alphaMode);
    _configuredWidth = _width;
    _configuredHeight = _height;
  }

  auto wgpuSurface = static_cast<WGPUSurface>(_surface);
  drawableProxy = std::make_shared<WebGPUDrawableProxy>(
      context, _width, _height, wgpuSurface, WGPUTextureFormat_BGRA8Unorm, PixelFormat::BGRA_8888);
  return drawableProxy;
}

void WebGPUWindow::onPresent(Context*) {
  if (drawableProxy == nullptr) {
    return;
  }
  auto proxy = std::static_pointer_cast<WebGPUDrawableProxy>(drawableProxy);
  proxy->present();
  proxy->releaseDrawable();
}

}  // namespace tgfx
