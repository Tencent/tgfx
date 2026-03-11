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
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Backend.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace tgfx {
CGLDrawable::CGLDrawable(NSOpenGLContext* glContext, NSView* view, int width, int height,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(width, height, std::move(colorSpace)), glContext(glContext), view(view) {
}

std::shared_ptr<RenderTargetProxy> CGLDrawable::getProxy(Context* context) {
  [glContext update];
  [glContext setView:view];
  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget renderTarget(frameBuffer, width(), height());
  return RenderTargetProxy::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
}

void CGLDrawable::onPresent(Context*) {
  [glContext flushBuffer];
}
}  // namespace tgfx

#pragma clang diagnostic pop
