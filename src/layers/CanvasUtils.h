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

#include <optional>
#include "tgfx/core/Rect.h"

namespace tgfx {

class Canvas;

/**
 * Returns the canvas clip bounds in the layer's local coordinate space, or std::nullopt if the
 * canvas cannot provide a meaningful clip (e.g. no canvas or no surface to derive bounds from).
 */
std::optional<Rect> GetClipBounds(const Canvas* canvas);

}  // namespace tgfx
