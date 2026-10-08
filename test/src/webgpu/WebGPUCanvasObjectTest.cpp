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

#include <emscripten/val.h>
#include "tgfx/gpu/webgpu/WebGPUDevice.h"
#include "tgfx/gpu/webgpu/WebGPUWindow.h"
#include "utils/TestUtils.h"

namespace tgfx {

static int ManagerEntryCount(const emscripten::val& manager) {
  auto keys = emscripten::val::global("Object").call<emscripten::val>("keys", manager["objects"]);
  return keys["length"].as<int>();
}

static emscripten::val MakeTestCanvas() {
  auto document = emscripten::val::global("document");
  auto canvas = document.call<emscripten::val>("createElement", emscripten::val("canvas"));
  canvas.set("width", 64);
  canvas.set("height", 64);
  return canvas;
}

static void SetCanvasWidthUndefined(const emscripten::val& canvas) {
  auto descriptor = emscripten::val::object();
  descriptor.set("value", emscripten::val::undefined());
  descriptor.set("configurable", true);
  emscripten::val::global("Object").call<emscripten::val>("defineProperty", canvas,
                                                          emscripten::val("width"), descriptor);
}

TGFX_TEST(WebGPUCanvasObjectTest, MakeFromReleasesImportedRuntimeEntries) {
  auto jsDevice = emscripten::val::module_property("preinitializedWebGPUDevice");
  auto webgpu = emscripten::val::module_property("WebGPU");
  if (!jsDevice.as<bool>() || !webgpu.as<bool>()) {
    GTEST_SKIP() << "WebGPU device is not available";
  }

  auto deviceEntries = webgpu["mgrDevice"];
  auto queueEntries = webgpu["mgrQueue"];
  const auto deviceCount = ManagerEntryCount(deviceEntries);
  const auto queueCount = ManagerEntryCount(queueEntries);
  for (int i = 0; i < 3; ++i) {
    auto device = WebGPUDevice::MakeFrom(jsDevice);
    ASSERT_TRUE(device != nullptr);
    device.reset();
    EXPECT_EQ(ManagerEntryCount(deviceEntries), deviceCount);
    EXPECT_EQ(ManagerEntryCount(queueEntries), queueCount);
  }

  auto queue = jsDevice["queue"];
  ASSERT_TRUE(queue.as<bool>());
  queue.call<void>("submit", emscripten::val::array());
}

TGFX_TEST(WebGPUCanvasObjectTest, MakeFromRejectsMissingImportBinding) {
  auto bindings = emscripten::val::module_property("tgfx");
  auto importWebGPUDevice = bindings["importWebGPUDevice"];
  ASSERT_TRUE(importWebGPUDevice.typeOf().strictlyEquals(emscripten::val("function")));

  bindings.set("importWebGPUDevice", emscripten::val::undefined());
  auto device = WebGPUDevice::MakeFrom(emscripten::val::object());
  bindings.set("importWebGPUDevice", importWebGPUDevice);

  EXPECT_TRUE(device == nullptr);
}

TGFX_TEST(WebGPUCanvasObjectTest, WindowRejectsMissingSurfaceBinding) {
  auto jsDevice = emscripten::val::module_property("preinitializedWebGPUDevice");
  if (!jsDevice.as<bool>()) {
    GTEST_SKIP() << "WebGPU device is not available";
  }
  auto device = WebGPUDevice::MakeFrom(jsDevice);
  ASSERT_TRUE(device != nullptr);

  auto bindings = emscripten::val::module_property("tgfx");
  auto createWebGPUSurface = bindings["createWebGPUSurface"];
  ASSERT_TRUE(createWebGPUSurface.typeOf().strictlyEquals(emscripten::val("function")));

  bindings.set("createWebGPUSurface", emscripten::val::undefined());
  auto window = WebGPUWindow::MakeFrom(emscripten::val::object(), device);
  bindings.set("createWebGPUSurface", createWebGPUSurface);

  EXPECT_TRUE(window == nullptr);
}

TGFX_TEST(WebGPUCanvasObjectTest, WindowRejectsInvalidCanvasSize) {
  auto jsDevice = emscripten::val::module_property("preinitializedWebGPUDevice");
  if (!jsDevice.as<bool>()) {
    GTEST_SKIP() << "WebGPU device is not available";
  }
  auto device = WebGPUDevice::MakeFrom(jsDevice);
  ASSERT_TRUE(device != nullptr);

  auto canvas = MakeTestCanvas();
  SetCanvasWidthUndefined(canvas);
  EXPECT_TRUE(WebGPUWindow::MakeFrom(canvas, device) == nullptr);

  auto largeCanvas = MakeTestCanvas();
  auto descriptor = emscripten::val::object();
  descriptor.set("value", 2147483648.0);
  descriptor.set("configurable", true);
  emscripten::val::global("Object").call<emscripten::val>("defineProperty", largeCanvas,
                                                          emscripten::val("width"), descriptor);
  EXPECT_TRUE(WebGPUWindow::MakeFrom(largeCanvas, device) == nullptr);
}

}  // namespace tgfx
