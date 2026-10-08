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
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/webgl/WebGLDevice.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"
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

TGFX_TEST(WebGLDeviceTest, MakeFromRejectsMissingJSBinding) {
  auto bindings = emscripten::val::module_property("tgfx");
  auto createCanvasContext = bindings["createCanvasContext"];
  ASSERT_TRUE(createCanvasContext.typeOf().strictlyEquals(emscripten::val("function")));

  bindings.set("createCanvasContext", emscripten::val::undefined());
  auto device = WebGLDevice::MakeFrom(MakeTestCanvas());
  bindings.set("createCanvasContext", createCanvasContext);

  EXPECT_TRUE(device == nullptr);
}

TGFX_TEST(WebGLDeviceTest, WindowRejectsInvalidCanvasSize) {
  auto canvas = MakeTestCanvas();
  auto window = WebGLWindow::MakeFrom(canvas);
  ASSERT_TRUE(window != nullptr);

  auto descriptor = emscripten::val::object();
  descriptor.set("value", emscripten::val::undefined());
  descriptor.set("configurable", true);
  emscripten::val::global("Object").call<emscripten::val>("defineProperty", canvas,
                                                          emscripten::val("width"), descriptor);

  auto device = window->getDevice();
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::MakeFrom(context, window);
  device->unlock();
  EXPECT_TRUE(surface == nullptr);

  auto largeCanvas = MakeTestCanvas();
  auto largeWindow = WebGLWindow::MakeFrom(largeCanvas);
  ASSERT_TRUE(largeWindow != nullptr);
  descriptor.set("value", 2147483648.0);
  emscripten::val::global("Object").call<emscripten::val>("defineProperty", largeCanvas,
                                                          emscripten::val("width"), descriptor);
  auto largeDevice = largeWindow->getDevice();
  auto largeContext = largeDevice->lockContext();
  ASSERT_TRUE(largeContext != nullptr);
  auto largeSurface = Surface::MakeFrom(largeContext, largeWindow);
  largeDevice->unlock();
  EXPECT_TRUE(largeSurface == nullptr);
}

}  // namespace tgfx
