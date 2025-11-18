/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <array>
#include <limits>
#include <optional>
#include <unordered_map>
#include "gpu/opengl/GLInterface.h"
#include "tgfx/core/Color.h"

namespace tgfx {
class GLTexture;
class GLRenderPipeline;

static constexpr unsigned INVALID_VALUE = std::numeric_limits<unsigned>::max();

enum class FrameBufferTarget { Draw, Read, Both };

struct GLStencil {
  unsigned compare = INVALID_VALUE;
  unsigned failOp = INVALID_VALUE;
  unsigned depthFailOp = INVALID_VALUE;
  unsigned passOp = INVALID_VALUE;

  bool operator!=(const GLStencil& other) const;
};

struct GLStencilState {
  GLStencil front = {};
  GLStencil back = {};
  unsigned readMask = INVALID_VALUE;
  unsigned writeMask = INVALID_VALUE;
  uint32_t reference = INVALID_VALUE;
};

struct GLDepthState {
  unsigned compare = INVALID_VALUE;
  unsigned writeMask = INVALID_VALUE;
};

struct GLBlendState {
  unsigned srcColorFactor = INVALID_VALUE;
  unsigned dstColorFactor = INVALID_VALUE;
  unsigned srcAlphaFactor = INVALID_VALUE;
  unsigned dstAlphaFactor = INVALID_VALUE;
  unsigned colorOp = INVALID_VALUE;
  unsigned alphaOp = INVALID_VALUE;
};

/**
 * GLState is used to cache and manage the OpenGL state to minimize redundant state changes.
 */
class GLState {
 public:
  explicit GLState(std::shared_ptr<GLInterface> glInterface);

  void setEnabled(unsigned capability, bool enabled);

  void setScissorRect(int x, int y, int width, int height);

  void setViewport(int x, int y, int width, int height);

  void setClearColor(PMColor color);

  void setColorMask(uint32_t colorMask);

  void setStencilState(const GLStencilState& state);

  void setDepthState(const GLDepthState& state);

  void setBlendState(const GLBlendState& state);

  void bindTexture(GLTexture* texture, unsigned textureUnit = 0);

  void bindFramebuffer(GLTexture* texture, FrameBufferTarget target = FrameBufferTarget::Both);

  void bindPipeline(GLRenderPipeline* pipeline);

  void reset();

 private:
  std::shared_ptr<GLInterface> interface = nullptr;
  std::unordered_map<unsigned, bool> capabilities = {};
  std::vector<uint32_t> textureUnits = {};
  std::array<int, 4> scissorRect = {0, 0, 0, 0};
  std::array<int, 4> viewport = {0, 0, 0, 0};
  std::optional<PMColor> clearColor = std::nullopt;
  uint32_t activePipeline = 0;
  unsigned activeTextureUint = INVALID_VALUE;
  unsigned readFramebuffer = INVALID_VALUE;
  unsigned drawFramebuffer = INVALID_VALUE;
  uint32_t colorWriteMask = INVALID_VALUE;
  GLStencilState stencilState = {};
  GLDepthState depthState = {};
  GLBlendState blendState = {};
};
}  // namespace tgfx
