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

#include "WebGLDrawable.h"
#include "gpu/opengl/GLDefines.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"

namespace tgfx {
WebGLDrawable::WebGLDrawable(int width, int height, std::shared_ptr<ColorSpace> colorSpace)
    : GLDrawable(width, height, std::move(colorSpace)) {
}

std::shared_ptr<RenderTarget> WebGLDrawable::onCreateRenderTarget(Context* context) {
  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget backendRT(frameBuffer, width(), height());
  return RenderTarget::MakeFrom(context, backendRT, ImageOrigin::BottomLeft);
}

void WebGLDrawable::onPresent(Context*) {
}
}  // namespace tgfx
