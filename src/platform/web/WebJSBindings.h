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

#pragma once

#include <emscripten/val.h>
#include <atomic>
#include <limits>
#include "core/utils/Log.h"

namespace tgfx {

inline bool HasWebJSBinding(const char* name) {
  auto bindings = emscripten::val::module_property("tgfx");
  if (!bindings.as<bool>()) {
    return false;
  }
  return bindings[name].typeOf().strictlyEquals(emscripten::val("function"));
}

inline bool ReadCanvasSize(const emscripten::val& canvas, int* width, int* height) {
  auto widthValue = canvas["width"];
  auto heightValue = canvas["height"];
  if (!widthValue.isNumber() || !heightValue.isNumber()) {
    return false;
  }
  auto widthNumber = widthValue.as<double>();
  auto heightNumber = heightValue.as<double>();
  constexpr auto minSize = static_cast<double>(std::numeric_limits<int>::min());
  constexpr auto maxSize = static_cast<double>(std::numeric_limits<int>::max());
  if (!(widthNumber >= minSize && widthNumber <= maxSize) ||
      !(heightNumber >= minSize && heightNumber <= maxSize)) {
    return false;
  }
  *width = static_cast<int>(widthNumber);
  *height = static_cast<int>(heightNumber);
  return true;
}

/**
 * Returns whether the JS bundle exports the entry that presents a frame on the calling thread.
 * Resolve it once per window; the answer cannot change afterwards.
 */
inline bool SupportsCanvasPresent() {
  return HasWebJSBinding("presentCanvasFrame");
}

/**
 * Asks the browser for one rendering update on this thread, which is when a canvas this thread owns
 * is presented. Only call it when SupportsCanvasPresent() holds. The GL runtime is used by engines
 * that predate worker animation frames.
 */
inline void PresentCanvasFrame(const emscripten::val& glRuntime = emscripten::val::undefined()) {
  emscripten::val::module_property("tgfx").call<void>("presentCanvasFrame", glRuntime);
}

}  // namespace tgfx
