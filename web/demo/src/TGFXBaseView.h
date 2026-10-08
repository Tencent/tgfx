/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <emscripten/bind.h>
#include "hello2d/AppHost.h"
#include "hello2d/LayerBuilder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/SurfaceReadback.h"
#include "tgfx/gpu/Recording.h"
#ifdef TGFX_USE_WEBGPU
#include "tgfx/gpu/webgpu/WebGPUWindow.h"
#else
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"
#endif
#include "tgfx/layers/DisplayList.h"

namespace hello2d {

class TGFXBaseView {
 public:
  TGFXBaseView(const std::string& canvasID);

  /** Creates a view for a non-null HTMLCanvasElement or OffscreenCanvas held by this thread. */
  TGFXBaseView(emscripten::val canvas);

  void setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage);

  void updateSize();

  /** Sets backing-store pixels per layout pixel, normally window.devicePixelRatio. */
  void setLayoutDensity(float density);

#ifdef TGFX_USE_WEBGPU
  /**
   * Sets or replaces the GPUDevice used by the view. Required on threads without a default device.
   * Changes take effect on the next updateSize() or draw(); the caller must keep the device alive.
   */
  void setWebGPUDevice(emscripten::val device);
#endif

  void updateLayerTree(int drawIndex);

  void updateZoomScaleAndOffset(float zoom, float offsetX, float offsetY);

  void draw();

  /** Starts pixel readback; returns pixels (WebGL), buffer metadata (WebGPU), or null on failure. */
  emscripten::val startReadback(int srcX, int srcY, int width, int height);

  /** Returns pixels after the WebGPU buffer is mapped, or null if readback failed. */
  emscripten::val finishReadback();

 protected:
  std::shared_ptr<hello2d::AppHost> appHost = nullptr;

 private:
  void applyCenteringTransform();

  // Shared by the constructor paths: builds the platform window from whichever of the canvas object
  // and the canvas id this view was created with.
  std::shared_ptr<tgfx::Window> createWindow();

  std::string canvasID = "";
  // Set only when the view was created from a canvas object, in which case canvasID is unused. Named
  // canvasVal so that it cannot be confused with the tgfx::Canvas that draw() works on.
  emscripten::val canvasVal;
  std::shared_ptr<tgfx::Window> window = nullptr;
  std::shared_ptr<tgfx::Surface> surface = nullptr;
  tgfx::DisplayList displayList = {};
  std::shared_ptr<tgfx::Layer> contentLayer = nullptr;
  int lastDrawIndex = -1;
  std::unique_ptr<tgfx::Recording> lastRecording = nullptr;
  int lastSurfaceWidth = 0;
  int lastSurfaceHeight = 0;
  bool presentImmediately = true;
  bool forceDraw = false;
  // Zero means "not pushed in yet", in which case draw() falls back to querying the DOM.
  float layoutDensity = 0.0f;
#ifdef TGFX_USE_WEBGPU
  // Set only when a device was pushed in, in which case it is used instead of the default device.
  emscripten::val webgpuDeviceVal;
  // The imported form of webgpuDeviceVal. Kept so retries reuse the same runtime registration and
  // released when the wrapper is destroyed. See createWindow().
  std::shared_ptr<tgfx::WebGPUDevice> webgpuDevice = nullptr;
#endif

  // Async readback state
  std::shared_ptr<tgfx::SurfaceReadback> pendingReadback = nullptr;
};

}  // namespace hello2d
