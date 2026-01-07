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
#include "tgfx/gpu/opengl/webgl/WebGLDevice.h"
#include <emscripten/val.h>
#include "core/utils/Log.h"
#include "gpu/opengl/webgl/WebGLGPU.h"

namespace tgfx {
enum class WebNamedColorSpace { None = 0, SRGB = 1, DisplayP3 = 2, Others = 3 };

void* GLDevice::CurrentNativeHandle() {
  return reinterpret_cast<void*>(emscripten_webgl_get_current_context());
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto context = emscripten_webgl_get_current_context();
  auto glDevice = GLDevice::Get(reinterpret_cast<void*>(context));
  if (glDevice) {
    return std::static_pointer_cast<WebGLDevice>(glDevice);
  }
  return WebGLDevice::Wrap(context, true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void*) {
  return nullptr;
}

std::shared_ptr<WebGLDevice> WebGLDevice::MakeFrom(const std::string& canvasID,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  static int i = 0;
  i++;
  ::tgfx::PrintError("WebGLDevice::MakeFrom call num: %d", i);
  auto oldContext = emscripten_webgl_get_current_context();

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.depth = EM_FALSE;
  attrs.stencil = EM_FALSE;
  attrs.antialias = EM_FALSE;
  attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
  attrs.enableExtensionsByDefault = EM_TRUE;
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  auto context = emscripten_webgl_create_context(canvasID.c_str(), &attrs);
  if (context == 0) {
    // fallback to WebGL 1.0
    ::tgfx::PrintError("WebGLDevice::MakeFrom call num: %d", i);
    attrs.majorVersion = 1;
    context = emscripten_webgl_create_context(canvasID.c_str(), &attrs);
    if (context == 0) {
      ::tgfx::PrintError("WebGLDevice::MakeFrom call num: %d", i);
      return nullptr;
    }
  }
  auto result = emscripten_webgl_make_context_current(context);
  if (result != EMSCRIPTEN_RESULT_SUCCESS) {
    emscripten_webgl_destroy_context(context);
    if (oldContext) {
      emscripten_webgl_make_context_current(oldContext);
    }
    return nullptr;
  }
  WebNamedColorSpace cs = WebNamedColorSpace::Others;
  if (colorSpace == nullptr) {
    cs = WebNamedColorSpace::None;
  } else if (ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    cs = WebNamedColorSpace::SRGB;
  } else if (ColorSpace::Equals(colorSpace.get(), ColorSpace::DisplayP3().get())) {
    cs = WebNamedColorSpace::DisplayP3;
  }
  bool isColorSpaceSupport = emscripten::val::module_property("tgfx").call<bool>(
      "setColorSpace", emscripten::val::module_property("GL"), static_cast<int>(cs));
  if (!isColorSpaceSupport) {
    LOGE(
        "WebGLDevice::MakeFrom() The specified ColorSpace is not supported on this platform. "
        "Rendering may have color inaccuracies.");
  }
  emscripten_webgl_make_context_current(0);
  if (oldContext) {
    emscripten_webgl_make_context_current(oldContext);
  }
  return WebGLDevice::Wrap(context, false);
}

std::shared_ptr<WebGLDevice> WebGLDevice::Wrap(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE webglContext,
                                               bool externallyOwned) {
  if (webglContext == 0) {
    return nullptr;
  }
  auto oldContext = emscripten_webgl_get_current_context();
  if (oldContext != webglContext) {
    auto result = emscripten_webgl_make_context_current(webglContext);
    if (result != EMSCRIPTEN_RESULT_SUCCESS) {
      emscripten_webgl_make_context_current(0);
      if (oldContext) {
        emscripten_webgl_make_context_current(oldContext);
      }
      return nullptr;
    }
  }
  std::shared_ptr<WebGLDevice> device = nullptr;
  auto interface = GLInterface::GetNative();
  if (interface != nullptr) {
    auto gpu = std::make_unique<WebGLGPU>(std::move(interface));
    device = std::shared_ptr<WebGLDevice>(new WebGLDevice(std::move(gpu), webglContext));
    device->externallyOwned = externallyOwned;
    device->context = webglContext;
    device->weakThis = device;
  }

  if (oldContext != webglContext) {
    emscripten_webgl_make_context_current(0);
    if (oldContext) {
      emscripten_webgl_make_context_current(oldContext);
    }
  }
  return device;
}

WebGLDevice::WebGLDevice(std::unique_ptr<GPU> gpu, EMSCRIPTEN_WEBGL_CONTEXT_HANDLE nativeHandle)
    : GLDevice(std::move(gpu), reinterpret_cast<void*>(nativeHandle)) {
}

WebGLDevice::~WebGLDevice() {
  releaseAll();
  if (externallyOwned) {
    return;
  }
  emscripten_webgl_destroy_context(context);
}

bool WebGLDevice::onLockContext() {
  oldContext = emscripten_webgl_get_current_context();
  if (oldContext == context) {
    return true;
  }
  auto result = emscripten_webgl_make_context_current(context);
  if (result != EMSCRIPTEN_RESULT_SUCCESS) {
    LOGE("WebGLDevice::onLockContext failure result = %d", result);
    return false;
  }
  return true;
}

void WebGLDevice::onUnlockContext() {
  emscripten_webgl_make_context_current(0);
  if (oldContext) {
    emscripten_webgl_make_context_current(oldContext);
  }
}

bool WebGLDevice::sharableWith(void* nativeContext) const {
  return nativeHandle == nativeContext;
}
}  // namespace tgfx
