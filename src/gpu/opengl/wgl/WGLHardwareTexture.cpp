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

#include "WGLHardwareTexture.h"
#include "WGLInteropState.h"
#include "core/utils/Log.h"
#include "platform/win/D3D11Util.h"
#include "tgfx/platform/HardwareBuffer.h"

// WGL_NV_DX_interop definitions
#define WGL_ACCESS_READ_ONLY_NV 0x0000

// GL_EXT_memory_object definitions
#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#define GL_HANDLE_TYPE_D3D11_IMAGE_KMT_EXT 0x958C

namespace tgfx {

// ============================================================================
// D3D11→GL import via memory_object (KMT handle)
// ============================================================================
static unsigned ImportViaMemoryObject(WGLGPU* gpu, HardwareBufferRef hardwareBuffer, int width,
                                      int height, unsigned glInternalFormat,
                                      unsigned* outMemoryObject) {
  auto* state = gpu->getInteropState();
  if (!state->glCreateMemoryObjectsEXT || !state->glImportMemoryWin32HandleEXT ||
      !state->glTexStorageMem2DEXT) {
    return 0;
  }

  // IID_IDXGIResource = {035f3ab4-482e-4e50-b41f-8a7f8bd8960b}
  static const GUID IID_IDXGIResource_local = {0x035f3ab4,
                                               0x482e,
                                               0x4e50,
                                               {0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b}};

  IUnknown* textureUnk = reinterpret_cast<IUnknown*>(hardwareBuffer);
  IUnknown* dxgiResource = nullptr;
  HRESULT hr =
      textureUnk->QueryInterface(IID_IDXGIResource_local, reinterpret_cast<void**>(&dxgiResource));
  if (FAILED(hr) || !dxgiResource) {
    LOGE("WGLHardwareTexture: Failed to get IDXGIResource, hr=0x%08X", hr);
    return 0;
  }

  typedef HRESULT(STDMETHODCALLTYPE * GetSharedHandleFn)(IUnknown * self, HANDLE * pSharedHandle);
  auto vtable = *reinterpret_cast<void***>(dxgiResource);
  auto getSharedHandle = reinterpret_cast<GetSharedHandleFn>(vtable[8]);

  HANDLE kmtHandle = nullptr;
  hr = getSharedHandle(dxgiResource, &kmtHandle);
  dxgiResource->Release();

  if (FAILED(hr) || !kmtHandle) {
    LOGE("WGLHardwareTexture: Failed to get KMT shared handle, hr=0x%08X", hr);
    return 0;
  }

  unsigned glTextureId = 0;
  unsigned glMemObj = 0;
  glGenTextures(1, &glTextureId);
  state->glCreateMemoryObjectsEXT(1, &glMemObj);
  if (glTextureId == 0 || glMemObj == 0) {
    if (glTextureId) {
      glDeleteTextures(1, &glTextureId);
    }
    if (state->glDeleteMemoryObjectsEXT && glMemObj) {
      state->glDeleteMemoryObjectsEXT(1, &glMemObj);
    }
    return 0;
  }

  int dedicated = GL_TRUE;
  state->glMemoryObjectParameterivEXT(glMemObj, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
  state->glImportMemoryWin32HandleEXT(glMemObj, 0, GL_HANDLE_TYPE_D3D11_IMAGE_KMT_EXT, kmtHandle);

  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    LOGE("WGLHardwareTexture: glImportMemoryWin32HandleEXT failed, GL error=0x%04X", err);
    glDeleteTextures(1, &glTextureId);
    state->glDeleteMemoryObjectsEXT(1, &glMemObj);
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, glTextureId);
  state->glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, glInternalFormat, width, height, glMemObj, 0);

  err = glGetError();
  if (err != GL_NO_ERROR) {
    LOGE("WGLHardwareTexture: glTexStorageMem2DEXT failed, GL error=0x%04X", err);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &glTextureId);
    state->glDeleteMemoryObjectsEXT(1, &glMemObj);
    return 0;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (outMemoryObject) {
    *outMemoryObject = glMemObj;
  } else {
    state->glDeleteMemoryObjectsEXT(1, &glMemObj);
  }
  return glTextureId;
}

// ============================================================================
// D3D11→GL import via WGL_NV_DX_interop
// ============================================================================
static unsigned ImportViaWglDX(WGLGPU* gpu, IUnknown* d3d11Device, HardwareBufferRef hardwareBuffer,
                               HANDLE* outInteropDevice, HANDLE* outInteropTexture) {
  auto* state = gpu->getInteropState();
  if (!state->wglDXRegisterObjectNV) {
    return 0;
  }

  HANDLE interopDev = gpu->acquireSharedInteropDevice(d3d11Device);
  if (!interopDev) {
    LOGE("WGLHardwareTexture: Failed to get shared GL interop device.");
    return 0;
  }

  unsigned glTextureId = 0;
  glGenTextures(1, &glTextureId);
  if (glTextureId == 0) {
    gpu->releaseSharedInteropDevice(interopDev, d3d11Device);
    return 0;
  }

  HANDLE interopTex =
      state->wglDXRegisterObjectNV(interopDev, reinterpret_cast<IUnknown*>(hardwareBuffer),
                                   glTextureId, GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);
  if (!interopTex) {
    LOGE("WGLHardwareTexture: Failed to register D3D11 texture with OpenGL.");
    glDeleteTextures(1, &glTextureId);
    gpu->releaseSharedInteropDevice(interopDev, d3d11Device);
    return 0;
  }

  if (!state->wglDXLockObjectsNV(interopDev, 1, &interopTex)) {
    LOGE("WGLHardwareTexture: Failed to lock D3D11 texture for GL access.");
    state->wglDXUnregisterObjectNV(interopDev, interopTex);
    glDeleteTextures(1, &glTextureId);
    gpu->releaseSharedInteropDevice(interopDev, d3d11Device);
    return 0;
  }

  if (outInteropDevice) {
    *outInteropDevice = interopDev;
  }
  if (outInteropTexture) {
    *outInteropTexture = interopTex;
  }
  return glTextureId;
}

// ============================================================================
// WGLHardwareTexture implementation
// ============================================================================

WGLHardwareTexture::WGLHardwareTexture(const TextureDescriptor& descriptor,
                                       HardwareBufferRef hardwareBuffer, unsigned target,
                                       unsigned textureID)
    : GLTexture(descriptor, target, textureID), hardwareBuffer(hardwareBuffer) {
  HardwareBufferRetain(hardwareBuffer);
}

WGLHardwareTexture::~WGLHardwareTexture() {
  HardwareBufferRelease(hardwareBuffer);
}

std::shared_ptr<WGLHardwareTexture> WGLHardwareTexture::MakeFrom(WGLGPU* gpu,
                                                                 HardwareBufferRef hardwareBuffer,
                                                                 uint32_t usage) {
  if (!gpu || !HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.format == HardwareBufferFormat::Unknown ||
      info.format == HardwareBufferFormat::YCBCR_420_SP) {
    return nullptr;
  }

  auto pixelFormat = PixelFormat::Unknown;
  unsigned glInternalFormat = GL_RGBA8;
  bool needSwizzle = false;
  switch (info.format) {
    case HardwareBufferFormat::BGRA_8888:
      pixelFormat = PixelFormat::BGRA_8888;
      needSwizzle = true;
      break;
    case HardwareBufferFormat::RGBA_8888:
      pixelFormat = PixelFormat::RGBA_8888;
      break;
    case HardwareBufferFormat::ALPHA_8:
      pixelFormat = PixelFormat::ALPHA_8;
      glInternalFormat = GL_R8;
      break;
    default:
      return nullptr;
  }

  TextureDescriptor descriptor = {info.width, info.height, pixelFormat, false, 1, usage};

  // Try memory_object path first.
  if (gpu->isMemoryObjectInteropAvailable()) {
    unsigned memObj = 0;
    unsigned glTextureId = ImportViaMemoryObject(gpu, hardwareBuffer, info.width, info.height,
                                                 glInternalFormat, &memObj);
    if (glTextureId != 0) {
      auto texture = gpu->makeResource<WGLHardwareTexture>(
          descriptor, hardwareBuffer, static_cast<unsigned>(GL_TEXTURE_2D), glTextureId);
      if (needSwizzle) {
        auto glState = gpu->state();
        glState->bindTexture(texture.get());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
      }
      texture->memoryObject = memObj;
      return texture;
    }
  }

  // Fall back to WGL_NV_DX_interop.
  if (gpu->isNVDXInteropAvailable()) {
    IUnknown* d3dDevice = D3D11GetDeviceFromTexture(reinterpret_cast<IUnknown*>(hardwareBuffer));
    if (!d3dDevice) {
      LOGE("WGLHardwareTexture: Failed to get D3D11 device from texture.");
      return nullptr;
    }
    HANDLE interopDev = nullptr;
    HANDLE interopTex = nullptr;
    unsigned glTextureId = ImportViaWglDX(gpu, d3dDevice, hardwareBuffer, &interopDev, &interopTex);
    if (glTextureId != 0) {
      auto texture = gpu->makeResource<WGLHardwareTexture>(
          descriptor, hardwareBuffer, static_cast<unsigned>(GL_TEXTURE_2D), glTextureId);
      texture->interopDevice = interopDev;
      texture->d3d11Device = d3dDevice;
      texture->interopTexture = interopTex;
      return texture;
    }
  }

  LOGE("WGLHardwareTexture: No D3D11-GL interop path available.");
  return nullptr;
}

void WGLHardwareTexture::onReleaseTexture(GLGPU* gpu) {
  auto* wglGpu = static_cast<WGLGPU*>(gpu);
  auto* state = wglGpu->getInteropState();

  // Cleanup wglDX interop.
  if (interopTexture) {
    if (state->wglDXUnlockObjectsNV && interopDevice) {
      state->wglDXUnlockObjectsNV(interopDevice, 1, &interopTexture);
    }
    if (state->wglDXUnregisterObjectNV && interopDevice) {
      state->wglDXUnregisterObjectNV(interopDevice, interopTexture);
    }
    interopTexture = nullptr;
  }
  if (interopDevice) {
    wglGpu->releaseSharedInteropDevice(interopDevice, d3d11Device);
    interopDevice = nullptr;
  }

  // Delete GL texture via base class.
  GLTexture::onReleaseTexture(gpu);

  // Cleanup memory object.
  if (memoryObject != 0 && state->glDeleteMemoryObjectsEXT) {
    state->glDeleteMemoryObjectsEXT(1, &memoryObject);
    memoryObject = 0;
  }
}

}  // namespace tgfx
