/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLVertexArray.h"
#include "gpu/ops/Op.h"

namespace tgfx {

class GLRenderPass : public RenderPass {
 public:
  explicit GLRenderPass(Context* context);

 protected:
  bool onBindProgramAndScissorClip(const ProgramInfo* programInfo, const Rect& drawBounds) override;
  void onDraw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) override;
  void onDrawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) override;
  void onClear(const Rect& scissor, Color color) override;

 private:
  std::shared_ptr<GLVertexArray> vertexArray = nullptr;
  std::shared_ptr<GLBuffer> sharedVertexBuffer = nullptr;

  void draw(const std::function<void()>& func);
};
}  // namespace tgfx
