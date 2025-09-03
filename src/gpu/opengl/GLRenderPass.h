/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "gpu/RenderPass.h"
#include "gpu/RenderPassDescriptor.h"
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLInterface.h"

namespace tgfx {

class GLRenderPass : public RenderPass {
 public:
  GLRenderPass(std::shared_ptr<GLInterface> interface, RenderPassDescriptor descriptor);

  void begin();

  void setScissorRect(int x, int y, int width, int height) override;

 protected:
  bool onBindProgram(const ProgramInfo* programInfo) override;
  bool onBindBuffers(GPUBuffer* indexBuffer, GPUBuffer* vertexBuffer, size_t vertexOffset) override;
  void onDraw(PrimitiveType primitiveType, size_t baseVertex, size_t count,
              bool drawIndexed) override;
  void onEnd() override;

 private:
  std::shared_ptr<GLInterface> interface = nullptr;

  void bindTexture(int unitIndex, GPUTexture* texture, SamplerState samplerState = {});
};
}  // namespace tgfx
