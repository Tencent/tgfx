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

#include "GLState.h"
#include "gpu/ColorWriteMask.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
bool GLStencil::operator!=(const GLStencil& other) const {
  return compare != other.compare || failOp != other.failOp || depthFailOp != other.depthFailOp ||
         passOp != other.passOp;
}

GLState::GLState(std::shared_ptr<GLInterface> glInterface) : interface(std::move(glInterface)) {
  auto shaderCaps = interface->caps()->shaderCaps();
  textureUnits.resize(static_cast<size_t>(shaderCaps->maxFragmentSamplers), INVALID_VALUE);
}

void GLState::setEnabled(unsigned capability, bool enabled) {
  auto result = capabilities.find(capability);
  if (result != capabilities.end() && result->second == enabled) {
    return;
  }
  auto gl = interface->functions();
  if (enabled) {
    gl->enable(capability);
  } else {
    gl->disable(capability);
  }
  capabilities[capability] = enabled;
}

void GLState::setScissorRect(int x, int y, int width, int height) {
  if (scissorRect[0] == x && scissorRect[1] == y && scissorRect[2] == width &&
      scissorRect[3] == height) {
    return;
  }
  auto gl = interface->functions();
  gl->scissor(x, y, width, height);
  scissorRect[0] = x;
  scissorRect[1] = y;
  scissorRect[2] = width;
  scissorRect[3] = height;
}

void GLState::setViewport(int x, int y, int width, int height) {
  if (viewport[0] == x && viewport[1] == y && viewport[2] == width && viewport[3] == height) {
    return;
  }
  auto gl = interface->functions();
  gl->viewport(x, y, width, height);
  viewport[0] = x;
  viewport[1] = y;
  viewport[2] = width;
  viewport[3] = height;
}

void GLState::setClearColor(Color color) {
  if (clearColor.has_value() && *clearColor == color) {
    return;
  }
  auto gl = interface->functions();
  gl->clearColor(color.red, color.green, color.blue, color.alpha);
  clearColor = color;
}

void GLState::setColorMask(uint32_t colorMask) {
  if (colorMask == colorWriteMask) {
    return;
  }
  auto gl = interface->functions();
  auto red = (colorMask & ColorWriteMask::RED) != 0;
  auto green = (colorMask & ColorWriteMask::GREEN) != 0;
  auto blue = (colorMask & ColorWriteMask::BLUE) != 0;
  auto alpha = (colorMask & ColorWriteMask::ALPHA) != 0;
  gl->colorMask(red, green, blue, alpha);
  colorWriteMask = colorMask;
}

void GLState::setStencilState(const GLStencilState& state) {
  auto funcChanged = state.front.compare != stencilState.front.compare ||
                     state.back.compare != stencilState.back.compare ||
                     state.readMask != stencilState.readMask ||
                     state.reference != stencilState.reference;
  auto maskChanged = state.writeMask != stencilState.writeMask;
  auto opChanged = state.front != stencilState.front || state.back != stencilState.back;
  if (!funcChanged && !maskChanged && !opChanged) {
    return;
  }
  auto gl = interface->functions();
  if (funcChanged) {
    gl->stencilFuncSeparate(GL_FRONT, state.front.compare, static_cast<int>(state.reference),
                            state.readMask);
    gl->stencilFuncSeparate(GL_BACK, state.back.compare, static_cast<int>(state.reference),
                            state.readMask);
  }
  if (maskChanged) {
    gl->stencilMask(state.writeMask);
  }
  if (opChanged) {
    gl->stencilOpSeparate(GL_FRONT, state.front.failOp, state.front.depthFailOp,
                          state.front.passOp);
    gl->stencilOpSeparate(GL_BACK, state.back.failOp, state.back.depthFailOp, state.back.passOp);
  }
  stencilState = state;
}

void GLState::setDepthState(const GLDepthState& state) {
  if (state.compare == depthState.compare && state.writeMask == depthState.writeMask) {
    return;
  }
  auto gl = interface->functions();
  if (state.compare != depthState.compare) {
    gl->depthFunc(state.compare);
  }
  if (state.writeMask != depthState.writeMask) {
    gl->depthMask(state.writeMask != 0);
  }
  depthState = state;
}

void GLState::setBlendState(const GLBlendState& state) {
  auto funcChanged = state.srcColorFactor != blendState.srcColorFactor ||
                     state.dstColorFactor != blendState.dstColorFactor ||
                     state.srcAlphaFactor != blendState.srcAlphaFactor ||
                     state.dstAlphaFactor != blendState.dstAlphaFactor;
  auto opChanged = state.colorOp != blendState.colorOp || state.alphaOp != blendState.alphaOp;
  if (!funcChanged && !opChanged) {
    return;
  }
  auto gl = interface->functions();
  if (funcChanged) {
    gl->blendFuncSeparate(state.srcColorFactor, state.dstColorFactor, state.srcAlphaFactor,
                          state.dstAlphaFactor);
  }
  if (opChanged) {
    gl->blendEquationSeparate(state.colorOp, state.alphaOp);
  }
  blendState = state;
}

void GLState::setCullFaceState(const GLCullFaceState& state) {
  if (state.cullFace == cullFaceState.cullFace && state.frontFace == cullFaceState.frontFace) {
    return;
  }

  auto gl = interface->functions();
  if (state.frontFace != cullFaceState.frontFace) {
    gl->frontFace(state.frontFace);
  }
  if (state.cullFace != cullFaceState.cullFace) {
    gl->cullFace(state.cullFace);
  }
  cullFaceState = state;
}

void GLState::bindTexture(GLTexture* texture, unsigned textureUnit) {
  DEBUG_ASSERT(texture != nullptr);
  DEBUG_ASSERT(texture->usage() & GPUTextureUsage::TEXTURE_BINDING);
  auto& uniqueID = textureUnits[textureUnit];
  if (uniqueID == texture->uniqueID) {
    return;
  }

  auto gl = interface->functions();
  if (activeTextureUint != textureUnit) {
    gl->activeTexture(static_cast<unsigned>(GL_TEXTURE0) + textureUnit);
    activeTextureUint = textureUnit;
  }
  gl->bindTexture(texture->target(), texture->textureID());
  uniqueID = texture->uniqueID;
}

void GLState::bindFramebuffer(GLTexture* texture, FrameBufferTarget target) {
  DEBUG_ASSERT(texture != nullptr);
  auto uniqueID = texture->uniqueID;
  unsigned frameBufferTarget = GL_FRAMEBUFFER;
  switch (target) {
    case FrameBufferTarget::Read:
      if (uniqueID == readFramebuffer) {
        return;
      }
      frameBufferTarget = GL_READ_FRAMEBUFFER;
      readFramebuffer = texture->uniqueID;
      break;
    case FrameBufferTarget::Draw:
      if (uniqueID == drawFramebuffer) {
        return;
      }
      frameBufferTarget = GL_DRAW_FRAMEBUFFER;
      drawFramebuffer = texture->uniqueID;
      break;
    case FrameBufferTarget::Both:
      if (uniqueID == drawFramebuffer && uniqueID == readFramebuffer) {
        return;
      }
      frameBufferTarget = GL_FRAMEBUFFER;
      readFramebuffer = drawFramebuffer = texture->uniqueID;
      break;
  }
  auto gl = interface->functions();
  gl->bindFramebuffer(frameBufferTarget, texture->frameBufferID());
}

void GLState::bindVertexArray(unsigned vao) {
  DEBUG_ASSERT(interface->caps()->vertexArrayObjectSupport);
  if (vertexArray == vao) {
    return;
  }
  auto gl = interface->functions();
  gl->bindVertexArray(vao);
  vertexArray = vao;
}

void GLState::useProgram(unsigned programID) {
  if (program == programID) {
    return;
  }
  auto gl = interface->functions();
  gl->useProgram(programID);
  program = programID;
}

void GLState::reset() {
  capabilities = {};
  scissorRect = {0, 0, 0, 0};
  viewport = {0, 0, 0, 0};
  textureUnits.assign(textureUnits.size(), INVALID_VALUE);
  clearColor = std::nullopt;
  activeTextureUint = INVALID_VALUE;
  readFramebuffer = INVALID_VALUE;
  drawFramebuffer = INVALID_VALUE;
  program = INVALID_VALUE;
  vertexArray = INVALID_VALUE;
  colorWriteMask = INVALID_VALUE;
  stencilState = {};
  depthState = {};
  blendState = {};
};
}  // namespace tgfx
