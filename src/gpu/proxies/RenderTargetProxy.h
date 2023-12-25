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

#include "TextureProxy.h"
#include "gpu/RenderTarget.h"

namespace tgfx {
/**
 * This class delays the acquisition of render targets until they are actually required.
 */
class RenderTargetProxy {
 public:
  /**
   * Creates a new RenderTargetProxy with an existing RenderTarget
   */
  static std::shared_ptr<RenderTargetProxy> MakeFrom(std::shared_ptr<RenderTarget> renderTarget);

  /**
   * Creates a new RenderTargetProxy with an existing Texture
   */
  static std::shared_ptr<RenderTargetProxy> MakeFrom(std::shared_ptr<Texture> texture,
                                                     int sampleCount = 1);

  /**
   * Creates a new RenderTargetProxy instance with specified context, with, height, format, sample
   * count and mipmap state.
   */
  static std::shared_ptr<RenderTargetProxy> Make(Context* context, int width, int height,
                                                 PixelFormat format, int sampleCount = 1,
                                                 bool mipMapped = false);

  virtual ~RenderTargetProxy() = default;

  /**
   * Returns the width of the render target.
   */
  virtual int width() const = 0;

  /**
   * Returns the height of the render target.
   */
  virtual int height() const = 0;

  /**
   * Returns the origin of the render target, either ImageOrigin::TopLeft or
   * ImageOrigin::BottomLeft.
   */
  virtual ImageOrigin origin() const = 0;

  /**
   * If we are instantiated and have a render target, return the sampleCount value of that render
   * target. Otherwise, returns the proxy's sampleCount value from creation time.
   */
  virtual int sampleCount() const = 0;

  /**
   * Returns the associated Context instance.
   */
  virtual Context* getContext() const = 0;

  /**
   * Returns the associated TextureProxy instance. Returns nullptr if the RenderTargetProxy is not
   * backed by a texture.
   */
  virtual std::shared_ptr<TextureProxy> getTextureProxy() const = 0;

  /**
   * Returns the backing Texture of the proxy. Returns nullptr if the proxy is not instantiated yet,
   * or it is not backed by a texture.
   */
  std::shared_ptr<Texture> getTexture() const;

  /**
   * Returns the RenderTarget of the proxy. Returns nullptr if the proxy is not instantiated yet.
   */
  std::shared_ptr<RenderTarget> getRenderTarget() const;

  /**
   * Returns true if the backing texture is instantiated.
   */
  bool isInstantiated() const;

  /**
   * Instantiates the backing texture, if necessary. Returns true if the backing texture is
   * instantiated.
   */
  bool instantiate();

 protected:
  std::shared_ptr<RenderTarget> renderTarget = nullptr;

  explicit RenderTargetProxy(std::shared_ptr<RenderTarget> renderTarget = nullptr);

  /**
   * Overrides to create a new RenderTarget associated with specified context.
   */
  virtual std::shared_ptr<RenderTarget> onMakeRenderTarget() = 0;
};
}  // namespace tgfx
