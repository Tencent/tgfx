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

#include "CGLDrawable.h"
#include "gpu/opengl/GLDefines.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace tgfx {
CGLDrawable::CGLDrawable(NSOpenGLContext* glContext, NSView* view, int width, int height,
                         std::shared_ptr<ColorSpace> colorSpace)
    : GLDrawable(width, height, std::move(colorSpace)), glContext(glContext), view(view) {
}

BackendRenderTarget CGLDrawable::onCreateBackendRenderTarget() {
  [glContext update];
  [glContext setView:view];
  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  return {frameBuffer, width(), height()};
}

void CGLDrawable::onPresent(Context*, std::shared_ptr<CommandBuffer>) {
  [glContext flushBuffer];
}
}  // namespace tgfx

#pragma clang diagnostic pop
