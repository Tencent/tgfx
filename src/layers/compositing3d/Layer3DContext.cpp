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

#include "Layer3DContext.h"
#include "Opaque3DContext.h"
#include "Render3DContext.h"
#include "layers/compositing3d/Context3DCompositor.h"

namespace tgfx {

std::shared_ptr<Layer3DContext> Layer3DContext::Make(bool opaqueMode, Context* context,
                                                     const Rect& renderRect, float contentScale,
                                                     std::shared_ptr<ColorSpace> colorSpace) {
  if (opaqueMode) {
    return std::make_shared<Opaque3DContext>(renderRect, contentScale, std::move(colorSpace));
  }
  auto compositor = std::make_shared<Context3DCompositor>(
      *context, static_cast<int>(renderRect.width()), static_cast<int>(renderRect.height()));
  return std::make_shared<Render3DContext>(std::move(compositor), renderRect, contentScale,
                                           std::move(colorSpace));
}

Layer3DContext::Layer3DContext(const Rect& renderRect, float contentScale,
                               std::shared_ptr<ColorSpace> colorSpace)
    : _renderRect(renderRect), _contentScale(contentScale), _colorSpace(std::move(colorSpace)) {
}

}  // namespace tgfx
