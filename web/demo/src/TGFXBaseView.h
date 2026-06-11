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

  void setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage);

  void updateSize();

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

  std::string canvasID = "";
  std::shared_ptr<tgfx::Window> window = nullptr;
  std::shared_ptr<tgfx::Surface> surface = nullptr;
  tgfx::DisplayList displayList = {};
  std::shared_ptr<tgfx::Layer> contentLayer = nullptr;
  int lastDrawIndex = -1;
  std::unique_ptr<tgfx::Recording> lastRecording = nullptr;
  int lastSurfaceWidth = 0;
  int lastSurfaceHeight = 0;
  bool presentImmediately = true;

  // Async readback state
  std::shared_ptr<tgfx::SurfaceReadback> pendingReadback = nullptr;
};

}  // namespace hello2d
