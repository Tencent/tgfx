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

#include "MtlTexture.h"
#include "MtlExternalTexture.h"
#include "MtlGPU.h"
#include "MtlDefines.h"

namespace tgfx {

static PixelFormat ToPixelFormat(MTLPixelFormat mtlFormat) {
  switch (mtlFormat) {
    case MTLPixelFormatR8Unorm:
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

std::shared_ptr<MtlTexture> MtlTexture::Make(MtlGPU* gpu, const TextureDescriptor& descriptor) {
  if (!gpu || descriptor.width <= 0 || descriptor.height <= 0) {
    return nullptr;
  }

  // Create Metal texture descriptor
  MTLTextureDescriptor* mtlDescriptor = [[MTLTextureDescriptor alloc] init];

  // Set basic properties
  mtlDescriptor.textureType = MTLTextureType2D;
  mtlDescriptor.pixelFormat = gpu->getMTLPixelFormat(descriptor.format);
  mtlDescriptor.width = static_cast<NSUInteger>(descriptor.width);
  mtlDescriptor.height = static_cast<NSUInteger>(descriptor.height);
  mtlDescriptor.depth = 1;
  mtlDescriptor.mipmapLevelCount = static_cast<NSUInteger>(descriptor.mipLevelCount);
  mtlDescriptor.sampleCount = static_cast<NSUInteger>(descriptor.sampleCount);
  mtlDescriptor.arrayLength = 1;

  // Set usage flags
  mtlDescriptor.usage = MtlDefines::ToMTLTextureUsage(descriptor.usage);

  // Check if this is a depth-stencil texture
  bool isDepthStencil = (descriptor.format == PixelFormat::DEPTH24_STENCIL8);

  // Handle multisampling and storage mode
  if (descriptor.sampleCount > 1) {
    mtlDescriptor.textureType = MTLTextureType2DMultisample;
    // MSAA textures must use Private storage mode on macOS
    mtlDescriptor.storageMode = MTLStorageModePrivate;
  } else if (isDepthStencil) {
    // Depth-stencil textures must use Private storage mode
    mtlDescriptor.storageMode = MTLStorageModePrivate;
  } else {
    // Use Shared for CPU access on non-MSAA textures
    mtlDescriptor.storageMode = MTLStorageModeShared;
  }

  // Create Metal texture (newTextureWithDescriptor returns a retained object)
  id<MTLTexture> mtlTexture = [gpu->device() newTextureWithDescriptor:mtlDescriptor];
  [mtlDescriptor release];
  if (!mtlTexture) {
    return nullptr;
  }

  return gpu->makeResource<MtlTexture>(descriptor, mtlTexture);
}

std::shared_ptr<MtlTexture> MtlTexture::MakeFrom(MtlGPU* gpu, id<MTLTexture> mtlTexture,
                                                 uint32_t usage, bool adopted) {
  if (!gpu || !mtlTexture) {
    return nullptr;
  }

  // Build descriptor from the Metal texture properties
  TextureDescriptor descriptor = {};
  descriptor.width = static_cast<int>(mtlTexture.width);
  descriptor.height = static_cast<int>(mtlTexture.height);
  descriptor.format = ToPixelFormat(mtlTexture.pixelFormat);
  descriptor.mipLevelCount = static_cast<int>(mtlTexture.mipmapLevelCount);
  descriptor.sampleCount = static_cast<int>(mtlTexture.sampleCount);
  descriptor.usage = usage;

  if (adopted) {
    // Take ownership of the texture (caller transfers ownership)
    return gpu->makeResource<MtlTexture>(descriptor, mtlTexture);
  }
  // External texture: do not take ownership, do not release on destruction
  return gpu->makeResource<MtlExternalTexture>(descriptor, mtlTexture);
}

MtlTexture::MtlTexture(const TextureDescriptor& descriptor, id<MTLTexture> mtlTexture)
    : Texture(descriptor), texture(mtlTexture) {
}

void MtlTexture::onRelease(MtlGPU*) {
  onReleaseTexture();
}

void MtlTexture::onReleaseTexture() {
  if (texture != nil) {
    [texture release];
    texture = nil;
  }
}

BackendTexture MtlTexture::getBackendTexture() const {
  MtlTextureInfo mtlInfo;
  mtlInfo.texture = (__bridge const void*)texture;
  mtlInfo.format = static_cast<unsigned>(texture.pixelFormat);
  return BackendTexture(mtlInfo, static_cast<int>(texture.width), static_cast<int>(texture.height));
}

BackendRenderTarget MtlTexture::getBackendRenderTarget() const {
  MtlTextureInfo mtlInfo;
  mtlInfo.texture = (__bridge const void*)texture;
  mtlInfo.format = static_cast<unsigned>(texture.pixelFormat);
  return BackendRenderTarget(mtlInfo, static_cast<int>(texture.width), static_cast<int>(texture.height));
}

}  // namespace tgfx