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

#include "gpu/GPUTexture.h"
#include "tgfx/core/Color.h"

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
   * Constructs a ColorAttachment with the specified texture, load action, store action, clear
   * value, and resolve texture.
   */
  ColorAttachment(GPUTexture* texture, LoadAction loadAction = LoadAction::DontCare,
                  StoreAction storeAction = StoreAction::Store,
                  Color clearValue = Color::Transparent(), GPUTexture* resolveTexture = nullptr)
      : texture(texture), loadAction(loadAction), storeAction(storeAction), clearValue(clearValue),
        resolveTexture(resolveTexture) {
  }

  /**
   * Returns the texture associated with this color attachment.
   */
  GPUTexture* texture = nullptr;

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
  Color clearValue = Color::Transparent();

  /**
   * The texture to resolve the color attachment into. This is used for multisampled textures.
   * If this is nullptr, the color attachment will not be resolved.
   */
  GPUTexture* resolveTexture = nullptr;
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
  DepthStencilAttachment(GPUTexture* texture, LoadAction loadAction = LoadAction::Clear,
                         StoreAction storeAction = StoreAction::DontCare,
                         float depthClearValue = 1.0f, bool depthReadOnly = false,
                         uint32_t stencilClearValue = 0, bool stencilReadOnly = false)
      : texture(texture), loadAction(loadAction), storeAction(storeAction),
        depthClearValue(depthClearValue), depthReadOnly(depthReadOnly),
        stencilClearValue(stencilClearValue), stencilReadOnly(stencilReadOnly) {
  }

  /**
   * Returns the texture associated with this attachment.
   */
  GPUTexture* texture = nullptr;

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
   * A convenience constructor that initializes a RenderPassDescriptor with a single color
   * attachment.
   * @param texture The texture to render to.
   * @param loadAction The action to perform at the start of the render pass.
   * @param storeAction The action to perform at the end of the render pass.
   * @param clearValue The color value to clear the attachment with if the load action is
   * LoadAction::Clear.
   * @param resolveTexture The texture to resolve the color attachment into. This is used for
   * multisampled textures. If this is nullptr, the color attachment will not be resolved.
   */
  RenderPassDescriptor(GPUTexture* texture, LoadAction loadAction = LoadAction::DontCare,
                       StoreAction storeAction = StoreAction::Store,
                       Color clearValue = Color::Transparent(),
                       GPUTexture* resolveTexture = nullptr) {
    colorAttachments.emplace_back(texture, loadAction, storeAction, clearValue, resolveTexture);
  }

  /**
   * A convenience constructor that initializes a RenderPassDescriptor with a single color
   * attachment and a resolve texture.
   * @param texture The texture to render to.
   * @param resolveTexture  The texture to resolve the color attachment into.
   */
  RenderPassDescriptor(GPUTexture* texture, GPUTexture* resolveTexture) {
    colorAttachments.emplace_back(texture, LoadAction::Load, StoreAction::Store,
                                  Color::Transparent(), resolveTexture);
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
}  // namespace tgfx
