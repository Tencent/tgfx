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
//  license is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <emscripten/val.h>
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

constexpr char PRESENT_CALLS[] = "__tgfxPresentCalls";

emscripten::val MakeTestCanvas() {
  auto canvas = emscripten::val::global("document")
                    .call<emscripten::val>("createElement", emscripten::val("canvas"));
  canvas.set("width", 64);
  canvas.set("height", 64);
  return canvas;
}

// Replaces the presentation entry with a counter, so a test can tell whether a window asked for a
// frame instead of letting the browser run one.
emscripten::val ReplacePresentBinding() {
  auto bindings = emscripten::val::module_property("tgfx");
  auto original = bindings["presentCanvasFrame"];
  emscripten::val::global("globalThis").set(PRESENT_CALLS, 0);
  auto counter =
      emscripten::val::global("Function").new_(emscripten::val("globalThis.__tgfxPresentCalls++"));
  bindings.set("presentCanvasFrame", counter);
  return original;
}

void RestorePresentBinding(const emscripten::val& original) {
  emscripten::val::module_property("tgfx").set("presentCanvasFrame", original);
}

int PresentCalls() {
  return emscripten::val::global("globalThis")[PRESENT_CALLS].as<int>();
}

// Draws one frame through the window so the frame reaches a submission.
void SubmitOneFrame(const std::shared_ptr<WebGLWindow>& window) {
  auto device = window->getDevice();
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::White());
  context->flushAndSubmit();
  device->unlock();
}

}  // namespace

TGFX_TEST(WebGLWindowTest, PresentsOncePerSubmission) {
  auto original = ReplacePresentBinding();
  auto window = WebGLWindow::MakeFrom(MakeTestCanvas());
  ASSERT_TRUE(window != nullptr);
  SubmitOneFrame(window);
  EXPECT_EQ(PresentCalls(), 1);
  SubmitOneFrame(window);
  EXPECT_EQ(PresentCalls(), 2);
  RestorePresentBinding(original);
}

TGFX_TEST(WebGLWindowTest, DocumentCanvasIsPresentedByTheBrowser) {
  auto canvas = MakeTestCanvas();
  canvas.set("id", "tgfx-window-present-test");
  auto body = emscripten::val::global("document")["body"];
  body.call<void>("appendChild", canvas);

  auto original = ReplacePresentBinding();
  auto window = WebGLWindow::MakeFrom("#tgfx-window-present-test");
  ASSERT_TRUE(window != nullptr);
  SubmitOneFrame(window);
  EXPECT_EQ(PresentCalls(), 0);

  RestorePresentBinding(original);
  window = nullptr;
  body.call<void>("removeChild", canvas);
}

TGFX_TEST(WebGLWindowTest, MissingPresentBindingKeepsTheWindowUsable) {
  auto bindings = emscripten::val::module_property("tgfx");
  auto original = bindings["presentCanvasFrame"];
  bindings.set("presentCanvasFrame", emscripten::val::undefined());
  auto window = WebGLWindow::MakeFrom(MakeTestCanvas());
  bindings.set("presentCanvasFrame", original);
  ASSERT_TRUE(window != nullptr);
  SubmitOneFrame(window);
}

}  // namespace tgfx
