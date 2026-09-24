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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <emscripten/val.h>
#include "tgfx/gpu/opengl/webgl/WebGLDevice.h"
#include "utils/TestUtils.h"

namespace tgfx {

// Every canvas here is created for the test alone, so these cases do not touch the canvas that
// DevicePool builds the shared device from.
static emscripten::val MakeTestCanvas() {
  auto document = emscripten::val::global("document");
  auto canvas = document.call<emscripten::val>("createElement", emscripten::val("canvas"));
  canvas.set("width", 64);
  canvas.set("height", 64);
  return canvas;
}

TGFX_TEST(WebGLDeviceTest, MakeFromCanvasObject) {
  auto canvas = MakeTestCanvas();
  auto device = WebGLDevice::MakeFrom(canvas);
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  device->unlock();
}

TGFX_TEST(WebGLDeviceTest, MakeFromRejectsUnusableCanvas) {
  EXPECT_TRUE(WebGLDevice::MakeFrom(emscripten::val::null()) == nullptr);
  EXPECT_TRUE(WebGLDevice::MakeFrom(emscripten::val::undefined()) == nullptr);

  auto canvas = MakeTestCanvas();
  // A canvas that already hands out a 2D context cannot hand out a WebGL one. The device cannot be
  // created, and the overload has to report that rather than letting the failure escape.
  canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  EXPECT_TRUE(WebGLDevice::MakeFrom(canvas) == nullptr);
}

}  // namespace tgfx
