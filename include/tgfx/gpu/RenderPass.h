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

#include "tgfx/core/Color.h"
#include "tgfx/gpu/GPUBuffer.h"
#include "tgfx/gpu/RenderPipeline.h"
#include "tgfx/gpu/Sampler.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
/**
 * Defines types of actions performed for an attachment at the start of a rendering pass.
 */
enum class LoadAction {
  /**
   * The GPU has permission to discard the existing contents of the attachment at the start of the
   * render pass, replacing them with arbitrary data.
   */
  DontCare,

  /**
   * The GPU preserves the existing contents of the attachment at the start of the render pass.
   */
  Load,

  /**
   * The GPU writes a value to every pixel in the attachment at the start of the render pass.
   */
  Clear
};

/**
 * Defines types of actions performed for an attachment at the end of a rendering pass.
 */
enum class StoreAction {
  /**
   * The GPU has permission to discard the rendered contents of the attachment at the end of the
   * render pass, replacing them with arbitrary data.
   */
  DontCare,

  /**
   * The GPU stores the rendered contents to the texture.
   */
  Store
};

/**
 * Describes a color attachment in a render pass.
 */
class ColorAttachment {
 public:
  /**
   * Default constructor for ColorAttachment.
   */
  ColorAttachment() = default;

  /**
   * Constructs a ColorAttachment with the specified texture, load action, store action, clear value,
   * and resolve texture.
   */
  explicit ColorAttachment(std::shared_ptr<Texture> texture,
                           LoadAction loadAction = LoadAction::DontCare,
                           StoreAction storeAction = StoreAction::Store,
                           PMColor clearValue = Color::Transparent().premultiply(),
                           std::shared_ptr<Texture> resolveTexture = nullptr)
      : texture(std::move(texture)), loadAction(loadAction), storeAction(storeAction),
        clearValue(clearValue), resolveTexture(std::move(resolveTexture)) {
  }

  /**
   * Returns the texture associated with this color attachment.
   */
  std::shared_ptr<Texture> texture = nullptr;

  /**
   * The action to perform at the start of the render pass.
   */
  LoadAction loadAction = LoadAction::DontCare;

  /**
   * The action to perform at the end of the render pass.
   */
  StoreAction storeAction = StoreAction::Store;

  /**
   * The color value to clear the attachment with if the load action is LoadAction::Clear.
   */
  PMColor clearValue = PMColor::Transparent();

  /**
   * The texture to resolve the color attachment into. This is used for multisampled textures.
   * If this is nullptr, the color attachment will not be resolved.
   */
  std::shared_ptr<Texture> resolveTexture = nullptr;
};

/**
 * Describes a depth-stencil attachment in a render pass.
 */
class DepthStencilAttachment {
 public:
  /**
   * Default constructor for DepthStencilAttachment.
   */
  DepthStencilAttachment() = default;

  /**
   * Constructs a DepthStencilAttachment with the specified texture, load action, store action,
   * depth clear value, depth read-only flag, stencil clear value, and stencil read-only flag.
   */
  explicit DepthStencilAttachment(std::shared_ptr<Texture> texture,
                                  LoadAction loadAction = LoadAction::Clear,
                                  StoreAction storeAction = StoreAction::DontCare,
                                  float depthClearValue = 1.0f, bool depthReadOnly = false,
                                  uint32_t stencilClearValue = 0, bool stencilReadOnly = false)
      : texture(std::move(texture)), loadAction(loadAction), storeAction(storeAction),
        depthClearValue(depthClearValue), depthReadOnly(depthReadOnly),
        stencilClearValue(stencilClearValue), stencilReadOnly(stencilReadOnly) {
  }

  /**
   * The texture associated with this depth-stencil attachment.
   */
  std::shared_ptr<Texture> texture = nullptr;

  /**
   * The action to perform at the start of the render pass.
   */
  LoadAction loadAction = LoadAction::Clear;

  /**
   * The action to perform at the end of the render pass.
   */
  StoreAction storeAction = StoreAction::DontCare;

  /**
   * The depth to use when clearing the depth attachment if the loadAction is LoadAction::Clear.
   */
  float depthClearValue = 1.0f;

  /**
   * If set to true, the depth component is read-only during the render pass.
   */
  bool depthReadOnly = false;

  /**
   * The value to use when clearing the stencil attachment if the loadAction is LoadAction::Clear.
   */
  uint32_t stencilClearValue = 0;

  /**
   * If set to true, the stencil component is read-only during the render pass.
   */
  bool stencilReadOnly = false;
};

/**
 * A group of render attachments that hold the results of a render pass.
 */
class RenderPassDescriptor {
 public:
  /**
   * Default constructor for RenderPassDescriptor.
   */
  RenderPassDescriptor() = default;

  /**
   * A convenience constructor that initializes a RenderPassDescriptor with a single color attachment.
   * @param texture The texture to render to.
   * @param loadAction The action to perform at the start of the render pass.
   * @param storeAction The action to perform at the end of the render pass.
   * @param clearValue The color value to clear the attachment with if the load action is
   * LoadAction::Clear.
   * @param resolveTexture The texture to resolve the color attachment into. This is used for
   * multisampled textures. If this is nullptr, the color attachment will not be resolved.
   */
  explicit RenderPassDescriptor(std::shared_ptr<Texture> texture,
                                LoadAction loadAction = LoadAction::DontCare,
                                StoreAction storeAction = StoreAction::Store,
                                PMColor clearValue = PMColor::Transparent(),
                                std::shared_ptr<Texture> resolveTexture = nullptr) {
    colorAttachments.emplace_back(std::move(texture), loadAction, storeAction, clearValue,
                                  std::move(resolveTexture));
  }

  /**
   * A convenience constructor that initializes a RenderPassDescriptor with a single color
   * attachment and a resolve texture.
   * @param texture The texture to render to.
   * @param resolveTexture  The texture to resolve the color attachment into.
   */
  RenderPassDescriptor(std::shared_ptr<Texture> texture, std::shared_ptr<Texture> resolveTexture) {
    colorAttachments.emplace_back(std::move(texture), LoadAction::Load, StoreAction::Store,
                                  PMColor::Transparent(), std::move(resolveTexture));
  }

  /**
   * An array of objects defining the color attachments that will be output to when executing this
   * render pass.
   */
  std::vector<ColorAttachment> colorAttachments = {};

  /**
   * An object defining the depth/stencil attachment that will be output to and tested against when
   * executing this render pass.
   */
  DepthStencilAttachment depthStencilAttachment = {};
};

/**
 * Geometric primitives used for drawing.
 */
enum class PrimitiveType {
  Triangles,
  TriangleStrip,
};

/**
 * Index formats for indexed drawing.
 */
enum class IndexFormat {
  UInt16,
  UInt32,
};

/**
 * RenderPass represents an interface for encoding a sequence of rendering commands into a command
 * buffer. A RenderPass is begun by calling CommandEncoder::beginRenderPass() with a valid
 * RenderPassDescriptor, and must be ended by calling end() before beginning a new render pass.
 */
class RenderPass {
 public:
  virtual ~RenderPass() = default;

  /**
   * Returns the GPU associated with this RenderPass.
   */
  virtual GPU* gpu() const = 0;

  /**
   * Sets the viewport used during the rasterization stage to linearly map from normalized device
   * coordinates to viewport coordinates.
   */
  virtual void setViewport(int x, int y, int width, int height) = 0;

  /**
   * Sets the scissor rectangle used during the rasterization stage. After transformation into
   * viewport coordinates any fragments that fall outside the scissor rectangle will be discarded.
   */
  virtual void setScissorRect(int x, int y, int width, int height) = 0;

  /**
   * Sets the render pipeline to be used for subsequent draw calls. The pipeline defines the shader
   * programs and fixed-function state used during rendering.
   */
  virtual void setPipeline(std::shared_ptr<RenderPipeline> pipeline) = 0;

  /**
   * Sets a GPUBuffer as a uniform buffer to a specified binding index in the shader's UBO table.
   */
  virtual void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                                size_t size) = 0;

  /**
   * Sets a texture and its sampler state to a specified binding index in the shader's texture table.
   */
  virtual void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                          std::shared_ptr<Sampler> sampler) = 0;

  /**
   * Sets a vertex buffer at the specified slot with an optional offset. Slot indices correspond to
   * the VertexBufferLayout array in the VertexDescriptor. Slot 0 is typically used for per-vertex
   * data, and slot 1 for per-instance data.
   * @param slot The slot index to bind the buffer to.
   * @param buffer The vertex buffer to bind. If nullptr, the call is ignored.
   * @param offset The byte offset into the buffer where data begins.
   */
  virtual void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                               size_t offset = 0) = 0;

  /**
   * Sets the current index buffer with its format.
   */
  virtual void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer,
                              IndexFormat format = IndexFormat::UInt16) = 0;

  /**
   * Sets the stencil reference value using during stencil tests with the "replace" stencil
   * operation (as set in the descriptor of the GPU::createRenderPipeline() method, in the
   * properties defining the various stencil operations).
   */
  virtual void setStencilReference(uint32_t reference) = 0;

  /**
   * Draws primitives based on the vertex buffers provided by setVertexBuffer().
   * @param primitiveType The type of primitive to draw.
   * @param vertexCount The number of vertices to draw.
   * @param instanceCount The number of instances to draw. Defaults to 1.
   * @param firstVertex The index of the first vertex to draw. Defaults to 0.
   * @param firstInstance The first instance to draw. Defaults to 0. Note: Not supported on
   * OpenGL/OpenGL ES backend, must be 0.
   */
  virtual void draw(PrimitiveType primitiveType, size_t vertexCount, size_t instanceCount = 1,
                    size_t firstVertex = 0, size_t firstInstance = 0) = 0;

  /**
   * Draws indexed primitives based on the index buffer provided by setIndexBuffer() and the vertex
   * buffers provided by setVertexBuffer().
   * @param primitiveType The type of primitive to draw.
   * @param indexCount The number of indices to draw.
   * @param instanceCount The number of instances to draw. Defaults to 1.
   * @param firstIndex The index of the first index to use. Defaults to 0.
   * @param baseVertex A value added to each index before reading from the vertex buffer. Defaults
   * to 0. Note: Not supported on OpenGL/OpenGL ES backend, must be 0.
   * @param firstInstance The first instance to draw. Defaults to 0. Note: Not supported on
   * OpenGL/OpenGL ES backend, must be 0.
   */
  virtual void drawIndexed(PrimitiveType primitiveType, size_t indexCount, size_t instanceCount = 1,
                           size_t firstIndex = 0, int32_t baseVertex = 0,
                           size_t firstInstance = 0) = 0;

  /**
   * Completes the current render pass. After calling this method, no further commands can be added
   * to the render pass, and a new render pass can be started by calling
   * CommandEncoder::beginRenderPass() again.
   */
  void end() {
    onEnd();
    isEnd = true;
  }

 protected:
  RenderPassDescriptor descriptor = {};

  explicit RenderPass(RenderPassDescriptor descriptor) : descriptor(std::move(descriptor)) {
  }

  virtual void onEnd() = 0;

 private:
  bool isEnd = false;

  friend class CommandEncoder;
};
}  // namespace tgfx
