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

  /**
   * Creates a view that renders into an existing canvas object rather than a canvas looked up by id.
   *
   * Use this when the view is created on a thread without a DOM, such as a worker that received an
   * OffscreenCanvas. The canvas has to stay alive for as long as the view does.
   *
   * @param canvas An HTMLCanvasElement or an OffscreenCanvas. Must not be null.
   */
  TGFXBaseView(emscripten::val canvas);

  void setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage);

  void updateSize();

  /**
   * Sets the ratio between the canvas backing store and its layout size. Needed when the view runs
   * where that ratio cannot be read from the page, such as a worker rendering into an
   * OffscreenCanvas. Until it is called the ratio falls back to 1 for a canvas object, and to the
   * page for a canvas id.
   *
   * @param density Backing store size divided by layout size, normally window.devicePixelRatio.
   */
  void setLayoutDensity(float density);

#ifdef TGFX_USE_WEBGPU
  /**
   * Supplies the GPUDevice the view renders with. Required when the view is created on a thread that
   * cannot obtain the default device, such as a worker. Must be called before the first updateSize()
   * or draw(); the device has to stay alive for as long as the view does.
   *
   * @param device A GPUDevice obtained from navigator.gpu, or null to use the default device.
   */
  void setWebGPUDevice(emscripten::val device);
#endif

  void updateLayerTree(int drawIndex);

  void updateZoomScaleAndOffset(float zoom, float offsetX, float offsetY);

  void draw();

  /**
   * Starts an async readback operation. Submits the GPU copy command and returns a handle
   * containing the buffer info needed for JS-side mapAsync. Returns an object with:
   *   bufferHandle: int (WGPUBuffer id for JS WebGPU.mgrBuffer.get())
   *   bufferSize: int
   *   width: int, height: int, rowBytes: int
   * Returns null/undefined if readback cannot be started.
   */
  emscripten::val startReadback(int srcX, int srcY, int width, int height);

  /**
   * Finishes a previously started readback. Assumes the buffer is already mapped (JS called
   * mapAsync and it resolved). Returns a Uint8Array with the pixel data, or null on failure.
   */
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
  // Zero means "not pushed in yet", in which case draw() falls back to querying the DOM.
  float layoutDensity = 0.0f;
#ifdef TGFX_USE_WEBGPU
  // Set only when a device was pushed in, in which case it is used instead of the default device.
  emscripten::val webgpuDeviceVal;
  // The imported form of webgpuDeviceVal. Kept so that the import, which registers the device and
  // its queue with the runtime for good, only happens once. See createWindow().
  std::shared_ptr<tgfx::WebGPUDevice> webgpuDevice = nullptr;
#endif

  // Async readback state
  std::shared_ptr<tgfx::SurfaceReadback> pendingReadback = nullptr;
};

}  // namespace hello2d
