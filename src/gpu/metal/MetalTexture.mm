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

#include "MetalTexture.h"
#include "MetalExternalTexture.h"
#include "MetalGPU.h"
#include "MetalDefines.h"

namespace tgfx {

static PixelFormat ToPixelFormat(MTLPixelFormat metalFormat) {
  switch (metalFormat) {
    case MTLPixelFormatR8Unorm:
      // Metal R8Unorm maps to both ALPHA_8 and GRAY_8, but reverse mapping can only return one.
      // GRAY_8 information is lost here; callers wrapping external textures should provide the
      // correct PixelFormat explicitly when the original format matters.
      return PixelFormat::ALPHA_8;
    case MTLPixelFormatRG8Unorm:
      return PixelFormat::RG_88;
    case MTLPixelFormatRGBA8Unorm:
      return PixelFormat::RGBA_8888;
    case MTLPixelFormatBGRA8Unorm:
      return PixelFormat::BGRA_8888;
#if TARGET_OS_OSX
    case MTLPixelFormatDepth24Unorm_Stencil8:
#endif
    case MTLPixelFormatDepth32Float_Stencil8:
      return PixelFormat::DEPTH24_STENCIL8;
    default:
      return PixelFormat::Unknown;
  }
}

std::shared_ptr<MetalTexture> MetalTexture::Make(MetalGPU* gpu, const TextureDescriptor& descriptor) {
  if (!gpu || descriptor.width <= 0 || descriptor.height <= 0) {
    return nullptr;
  }

  // Create Metal texture descriptor
  MTLTextureDescriptor* metalDescriptor = [[MTLTextureDescriptor alloc] init];

  // Set basic properties
  metalDescriptor.textureType = MTLTextureType2D;
  metalDescriptor.pixelFormat = gpu->getMTLPixelFormat(descriptor.format);
  metalDescriptor.width = static_cast<NSUInteger>(descriptor.width);
  metalDescriptor.height = static_cast<NSUInteger>(descriptor.height);
  metalDescriptor.depth = 1;
  metalDescriptor.mipmapLevelCount = static_cast<NSUInteger>(descriptor.mipLevelCount);
  metalDescriptor.sampleCount = static_cast<NSUInteger>(descriptor.sampleCount);
  metalDescriptor.arrayLength = 1;

  // Set usage flags
  metalDescriptor.usage = MetalDefines::ToMTLTextureUsage(descriptor.usage);

  // Check if this is a depth-stencil texture
  bool isDepthStencil = (descriptor.format == PixelFormat::DEPTH24_STENCIL8);

  // Handle multisampling and storage mode
  if (descriptor.sampleCount > 1) {
    metalDescriptor.textureType = MTLTextureType2DMultisample;
    // MSAA textures must use Private storage mode on macOS
    metalDescriptor.storageMode = MTLStorageModePrivate;
  } else if (isDepthStencil) {
    // Depth-stencil textures must use Private storage mode
    metalDescriptor.storageMode = MTLStorageModePrivate;
  } else {
    // Use Shared for CPU access on non-MSAA textures
    metalDescriptor.storageMode = MTLStorageModeShared;
  }

  // Create Metal texture (newTextureWithDescriptor returns a retained object)
  id<MTLTexture> metalTexture = [gpu->device() newTextureWithDescriptor:metalDescriptor];
  [metalDescriptor release];
  if (!metalTexture) {
    return nullptr;
  }

  return gpu->makeResource<MetalTexture>(descriptor, metalTexture);
}

std::shared_ptr<MetalTexture> MetalTexture::MakeFrom(MetalGPU* gpu, id<MTLTexture> metalTexture,
                                                 uint32_t usage, bool adopted) {
  if (!gpu || !metalTexture) {
    return nullptr;
  }

  // Build descriptor from the Metal texture properties
  TextureDescriptor descriptor = {};
  descriptor.width = static_cast<int>(metalTexture.width);
  descriptor.height = static_cast<int>(metalTexture.height);
  descriptor.format = ToPixelFormat(metalTexture.pixelFormat);
  descriptor.mipLevelCount = static_cast<int>(metalTexture.mipmapLevelCount);
  descriptor.sampleCount = static_cast<int>(metalTexture.sampleCount);
  descriptor.usage = usage;

  if (adopted) {
    // Take ownership of the texture (caller transfers ownership)
    return gpu->makeResource<MetalTexture>(descriptor, metalTexture);
  }
  // External texture: do not take ownership, do not release on destruction
  return gpu->makeResource<MetalExternalTexture>(descriptor, metalTexture);
}

MetalTexture::MetalTexture(const TextureDescriptor& descriptor, id<MTLTexture> metalTexture)
    : Texture(descriptor), texture(metalTexture) {
}

void MetalTexture::onRelease(MetalGPU*) {
  onReleaseTexture();
}

void MetalTexture::onReleaseTexture() {
  if (texture != nil) {
    [texture release];
    texture = nil;
  }
}

BackendTexture MetalTexture::getBackendTexture() const {
  if (texture == nil || !(descriptor.usage & TextureUsage::TEXTURE_BINDING)) {
    return {};
  }
  MetalTextureInfo metalInfo;
  metalInfo.texture = (__bridge const void*)texture;
  metalInfo.format = static_cast<unsigned>(texture.pixelFormat);
  return BackendTexture(metalInfo, static_cast<int>(texture.width), static_cast<int>(texture.height));
}

BackendRenderTarget MetalTexture::getBackendRenderTarget() const {
  if (texture == nil || !(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    return {};
  }
  MetalTextureInfo metalInfo;
  metalInfo.texture = (__bridge const void*)texture;
  metalInfo.format = static_cast<unsigned>(texture.pixelFormat);
  return BackendRenderTarget(metalInfo, static_cast<int>(texture.width), static_cast<int>(texture.height));
}

}  // namespace tgfx
