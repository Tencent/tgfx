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
#include <windows.h>
#include <GL/gl.h>
#include <cstring>
#include <mutex>
#include <vector>
#include "core/utils/Log.h"
#include "tgfx/platform/HardwareBuffer.h"

// WGL_NV_DX_interop definitions
#define WGL_ACCESS_READ_ONLY_NV 0x0000

// GL_EXT_memory_object definitions
#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#define GL_HANDLE_TYPE_D3D11_IMAGE_KMT_EXT 0x958C

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE_SWIZZLE_R
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#endif
#ifndef GL_TEXTURE_SWIZZLE_B
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#endif

// WGL_NV_DX_interop function typedefs
typedef HANDLE(WINAPI* PFNWGLDXOPENDEVICENVPROC)(void* dxDevice);
typedef BOOL(WINAPI* PFNWGLDXCLOSEDEVICENVPROC)(HANDLE hDevice);
typedef HANDLE(WINAPI* PFNWGLDXREGISTEROBJECTNVPROC)(HANDLE hDevice, void* dxObject, GLuint name,
                                                     GLenum type, GLenum access);
typedef BOOL(WINAPI* PFNWGLDXUNREGISTEROBJECTNVPROC)(HANDLE hDevice, HANDLE hObject);
typedef BOOL(WINAPI* PFNWGLDXLOCKOBJECTSNVPROC)(HANDLE hDevice, GLint count, HANDLE* hObjects);
typedef BOOL(WINAPI* PFNWGLDXUNLOCKOBJECTSNVPROC)(HANDLE hDevice, GLint count, HANDLE* hObjects);

typedef const char*(WINAPI* PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC hdc);

// GL_EXT_memory_object function typedefs
typedef void(GL_FUNCTION_TYPE* PFNGLCREATEMEMORYOBJECTSEXTPROC)(int n, unsigned* memoryObjects);
typedef void(GL_FUNCTION_TYPE* PFNGLDELETEMEMORYOBJECTSEXTPROC)(int n,
                                                                const unsigned* memoryObjects);
typedef void(GL_FUNCTION_TYPE* PFNGLMEMORYOBJECTPARAMETERIVEXTPROC)(unsigned memoryObject,
                                                                    unsigned pname,
                                                                    const int* params);
typedef void(GL_FUNCTION_TYPE* PFNGLTEXSTORAGEMEM2DEXTPROC)(unsigned target, int levels,
                                                            unsigned internalFormat, int width,
                                                            int height, unsigned memory,
                                                            uint64_t offset);
typedef void(GL_FUNCTION_TYPE* PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)(unsigned memory, uint64_t size,
                                                                    unsigned handleType,
                                                                    void* handle);

namespace tgfx {

// ============================================================================
// WGL_NV_DX_interop state
// ============================================================================
static PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV = nullptr;
static PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV = nullptr;
static PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV = nullptr;
static PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV = nullptr;
static PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV = nullptr;
static PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV = nullptr;

static std::mutex g_nvInteropMutex;
static bool g_nvInteropChecked = false;
static bool g_nvInteropAvailable = false;

// ============================================================================
// GL_EXT_memory_object state
// ============================================================================
static PFNGLCREATEMEMORYOBJECTSEXTPROC glCreateMemoryObjectsEXT = nullptr;
static PFNGLDELETEMEMORYOBJECTSEXTPROC glDeleteMemoryObjectsEXT = nullptr;
static PFNGLMEMORYOBJECTPARAMETERIVEXTPROC glMemoryObjectParameterivEXT = nullptr;
static PFNGLTEXSTORAGEMEM2DEXTPROC glTexStorageMem2DEXT = nullptr;
static PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC glImportMemoryWin32HandleEXT = nullptr;

static std::mutex g_memObjInteropMutex;
static bool g_memObjInteropChecked = false;
static bool g_memObjInteropAvailable = false;

// ============================================================================
// Shared wglDX interop device (one per D3D11Device)
// ============================================================================
struct SharedInteropDevice {
  void* interopDevice = nullptr;
  void* d3d11Device = nullptr;
  int refCount = 0;
};

static std::mutex g_sharedInteropMutex;
static std::vector<SharedInteropDevice> g_sharedInteropDevices;

static void* AcquireSharedInteropDevice(void* d3d11Device) {
  if (!wglDXOpenDeviceNV || !d3d11Device) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(g_sharedInteropMutex);
  for (auto& shared : g_sharedInteropDevices) {
    if (shared.d3d11Device == d3d11Device && shared.interopDevice != nullptr) {
      shared.refCount++;
      return shared.interopDevice;
    }
  }
  void* interopDev = wglDXOpenDeviceNV(d3d11Device);
  if (!interopDev) {
    return nullptr;
  }
  g_sharedInteropDevices.push_back({interopDev, d3d11Device, 1});
  return interopDev;
}

static void ReleaseSharedInteropDevice(void* interopDev, void* d3d11Device) {
  if (!interopDev) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_sharedInteropMutex);
  for (auto it = g_sharedInteropDevices.begin(); it != g_sharedInteropDevices.end(); ++it) {
    if (it->interopDevice == interopDev && it->d3d11Device == d3d11Device) {
      it->refCount--;
      if (it->refCount <= 0) {
        if (wglDXCloseDeviceNV) {
          wglDXCloseDeviceNV(interopDev);
        }
        g_sharedInteropDevices.erase(it);
      }
      return;
    }
  }
  if (wglDXCloseDeviceNV) {
    wglDXCloseDeviceNV(interopDev);
  }
}

// ============================================================================
// Extension detection helpers
// ============================================================================
static bool HasExtension(const char* extensions, const char* extension) {
  if (!extensions || !extension) {
    return false;
  }
  const char* start = extensions;
  size_t extLen = strlen(extension);
  const char* where;
  while ((where = strstr(start, extension)) != nullptr) {
    const char* terminator = where + extLen;
    if ((where == start || *(where - 1) == ' ') && (*terminator == ' ' || *terminator == '\0')) {
      return true;
    }
    start = terminator;
  }
  return false;
}

static bool CheckNVDXInteropWithContext() {
  wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)wglGetProcAddress("wglDXOpenDeviceNV");
  wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)wglGetProcAddress("wglDXCloseDeviceNV");
  wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXRegisterObjectNV");
  wglDXUnregisterObjectNV =
      (PFNWGLDXUNREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXUnregisterObjectNV");
  wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXLockObjectsNV");
  wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXUnlockObjectsNV");
  return wglDXOpenDeviceNV && wglDXCloseDeviceNV && wglDXRegisterObjectNV &&
         wglDXUnregisterObjectNV && wglDXLockObjectsNV && wglDXUnlockObjectsNV;
}

static bool CheckNVDXInteropWithTempContext() {
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc = DefWindowProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = TEXT("TGFXWGLTempWindow");
  if (!RegisterClassEx(&wc)) {
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
  }
  HWND tempHwnd =
      CreateWindowEx(0, TEXT("TGFXWGLTempWindow"), TEXT("TGFX WGL Temp"), WS_OVERLAPPEDWINDOW, 0, 0,
                     1, 1, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
  if (!tempHwnd) {
    return false;
  }
  HDC tempDC = GetDC(tempHwnd);
  if (!tempDC) {
    DestroyWindow(tempHwnd);
    return false;
  }
  PIXELFORMATDESCRIPTOR tempPfd = {};
  tempPfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
  tempPfd.nVersion = 1;
  tempPfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  tempPfd.iPixelType = PFD_TYPE_RGBA;
  tempPfd.cColorBits = 32;
  tempPfd.cDepthBits = 24;
  tempPfd.cStencilBits = 8;
  tempPfd.iLayerType = PFD_MAIN_PLANE;
  int tempFormat = ChoosePixelFormat(tempDC, &tempPfd);
  if (tempFormat == 0 || !SetPixelFormat(tempDC, tempFormat, &tempPfd)) {
    ReleaseDC(tempHwnd, tempDC);
    DestroyWindow(tempHwnd);
    return false;
  }
  HGLRC tempContext = wglCreateContext(tempDC);
  if (!tempContext) {
    ReleaseDC(tempHwnd, tempDC);
    DestroyWindow(tempHwnd);
    return false;
  }
  if (!wglMakeCurrent(tempDC, tempContext)) {
    wglDeleteContext(tempContext);
    ReleaseDC(tempHwnd, tempDC);
    DestroyWindow(tempHwnd);
    return false;
  }
  auto wglGetExtensionsStringARB =
      (PFNWGLGETEXTENSIONSSTRINGARBPROC)wglGetProcAddress("wglGetExtensionsStringARB");
  bool hasExtension = false;
  if (wglGetExtensionsStringARB) {
    const char* extensions = wglGetExtensionsStringARB(tempDC);
    hasExtension = extensions && HasExtension(extensions, "WGL_NV_DX_interop");
  }
  bool result = hasExtension && CheckNVDXInteropWithContext();
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(tempContext);
  ReleaseDC(tempHwnd, tempDC);
  DestroyWindow(tempHwnd);
  return result;
}

static bool IsNVDXInteropAvailable() {
  std::lock_guard<std::mutex> lock(g_nvInteropMutex);
  if (g_nvInteropChecked) {
    return g_nvInteropAvailable;
  }
  g_nvInteropChecked = true;
  HGLRC currentContext = wglGetCurrentContext();
  g_nvInteropAvailable =
      currentContext ? CheckNVDXInteropWithContext() : CheckNVDXInteropWithTempContext();
  return g_nvInteropAvailable;
}

static bool CheckMemoryObjectInteropWithContext() {
  glCreateMemoryObjectsEXT =
      (PFNGLCREATEMEMORYOBJECTSEXTPROC)wglGetProcAddress("glCreateMemoryObjectsEXT");
  glDeleteMemoryObjectsEXT =
      (PFNGLDELETEMEMORYOBJECTSEXTPROC)wglGetProcAddress("glDeleteMemoryObjectsEXT");
  glMemoryObjectParameterivEXT =
      (PFNGLMEMORYOBJECTPARAMETERIVEXTPROC)wglGetProcAddress("glMemoryObjectParameterivEXT");
  glTexStorageMem2DEXT = (PFNGLTEXSTORAGEMEM2DEXTPROC)wglGetProcAddress("glTexStorageMem2DEXT");
  glImportMemoryWin32HandleEXT =
      (PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)wglGetProcAddress("glImportMemoryWin32HandleEXT");
  return glCreateMemoryObjectsEXT && glDeleteMemoryObjectsEXT && glMemoryObjectParameterivEXT &&
         glTexStorageMem2DEXT && glImportMemoryWin32HandleEXT;
}

static bool IsMemoryObjectInteropAvailable() {
  std::lock_guard<std::mutex> lock(g_memObjInteropMutex);
  if (g_memObjInteropChecked) {
    return g_memObjInteropAvailable;
  }
  HGLRC currentContext = wglGetCurrentContext();
  if (!currentContext) {
    return false;
  }
  g_memObjInteropChecked = true;

  typedef const unsigned char*(GL_FUNCTION_TYPE * PFNGLGETSTRINGIPROC)(unsigned name,
                                                                       unsigned index);
  auto glGetStringi = (PFNGLGETSTRINGIPROC)wglGetProcAddress("glGetStringi");
  if (!glGetStringi) {
    g_memObjInteropAvailable = false;
    return false;
  }
  int numExtensions = 0;
  glGetIntegerv(0x821D, &numExtensions);  // GL_NUM_EXTENSIONS
  bool hasMemObj = false, hasMemObjWin32 = false;
  for (int i = 0; i < numExtensions; i++) {
    const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
    if (ext) {
      if (strcmp(ext, "GL_EXT_memory_object") == 0) hasMemObj = true;
      else if (strcmp(ext, "GL_EXT_memory_object_win32") == 0)
        hasMemObjWin32 = true;
    }
    if (hasMemObj && hasMemObjWin32) break;
  }
  if (!hasMemObj || !hasMemObjWin32) {
    g_memObjInteropAvailable = false;
    return false;
  }
  g_memObjInteropAvailable = CheckMemoryObjectInteropWithContext();
  return g_memObjInteropAvailable;
}

// ============================================================================
// D3D11→GL import via memory_object (KMT handle)
// ============================================================================
static unsigned ImportViaMemoryObject(void* d3d11Texture, int width, int height,
                                      unsigned* outMemoryObject) {
  if (!glCreateMemoryObjectsEXT || !glImportMemoryWin32HandleEXT || !glTexStorageMem2DEXT) {
    return 0;
  }

  // IID_IDXGIResource = {035f3ab4-482e-4e50-b41f-8a7f8bd8960b}
  static const GUID IID_IDXGIResource_local = {0x035f3ab4,
                                               0x482e,
                                               0x4e50,
                                               {0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b}};

  IUnknown* textureUnk = static_cast<IUnknown*>(d3d11Texture);
  IUnknown* dxgiResource = nullptr;
  HRESULT hr =
      textureUnk->QueryInterface(IID_IDXGIResource_local, reinterpret_cast<void**>(&dxgiResource));
  if (FAILED(hr) || !dxgiResource) {
    LOGE("WGLHardwareTexture: Failed to get IDXGIResource, hr=0x%08X", hr);
    return 0;
  }

  typedef HRESULT(STDMETHODCALLTYPE * GetSharedHandleFn)(IUnknown * self, HANDLE * pSharedHandle);
  auto vtable = *reinterpret_cast<void***>(dxgiResource);
  auto GetSharedHandle = reinterpret_cast<GetSharedHandleFn>(vtable[8]);

  HANDLE kmtHandle = nullptr;
  hr = GetSharedHandle(dxgiResource, &kmtHandle);
  dxgiResource->Release();

  if (FAILED(hr) || !kmtHandle) {
    LOGE("WGLHardwareTexture: Failed to get KMT shared handle, hr=0x%08X", hr);
    return 0;
  }

  unsigned glTextureId = 0;
  unsigned glMemObj = 0;
  glGenTextures(1, &glTextureId);
  glCreateMemoryObjectsEXT(1, &glMemObj);
  if (glTextureId == 0 || glMemObj == 0) {
    if (glTextureId) glDeleteTextures(1, &glTextureId);
    if (glDeleteMemoryObjectsEXT && glMemObj) glDeleteMemoryObjectsEXT(1, &glMemObj);
    return 0;
  }

  int dedicated = GL_TRUE;
  glMemoryObjectParameterivEXT(glMemObj, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
  glImportMemoryWin32HandleEXT(glMemObj, 0, GL_HANDLE_TYPE_D3D11_IMAGE_KMT_EXT, kmtHandle);

  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    LOGE("WGLHardwareTexture: glImportMemoryWin32HandleEXT failed, GL error=0x%04X", err);
    glDeleteTextures(1, &glTextureId);
    glDeleteMemoryObjectsEXT(1, &glMemObj);
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, glTextureId);
  glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, width, height, glMemObj, 0);

  err = glGetError();
  if (err != GL_NO_ERROR) {
    LOGE("WGLHardwareTexture: glTexStorageMem2DEXT failed, GL error=0x%04X", err);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &glTextureId);
    glDeleteMemoryObjectsEXT(1, &glMemObj);
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
    glDeleteMemoryObjectsEXT(1, &glMemObj);
  }
  return glTextureId;
}

// ============================================================================
// D3D11→GL import via WGL_NV_DX_interop
// ============================================================================
static unsigned ImportViaWglDX(void* d3d11Device, void* d3d11Texture, void** outInteropDevice,
                               void** outInteropTexture) {
  if (!wglDXRegisterObjectNV) {
    return 0;
  }

  void* interopDev = AcquireSharedInteropDevice(d3d11Device);
  if (!interopDev) {
    LOGE("WGLHardwareTexture: Failed to get shared GL interop device.");
    return 0;
  }

  unsigned glTextureId = 0;
  glGenTextures(1, &glTextureId);
  if (glTextureId == 0) {
    ReleaseSharedInteropDevice(interopDev, d3d11Device);
    return 0;
  }

  void* interopTex = wglDXRegisterObjectNV(interopDev, d3d11Texture, glTextureId, GL_TEXTURE_2D,
                                           WGL_ACCESS_READ_ONLY_NV);
  if (!interopTex) {
    LOGE("WGLHardwareTexture: Failed to register D3D11 texture with OpenGL.");
    glDeleteTextures(1, &glTextureId);
    ReleaseSharedInteropDevice(interopDev, d3d11Device);
    return 0;
  }

  if (!wglDXLockObjectsNV(interopDev, 1, reinterpret_cast<HANDLE*>(&interopTex))) {
    LOGE("WGLHardwareTexture: Failed to lock D3D11 texture for GL access.");
    wglDXUnregisterObjectNV(interopDev, interopTex);
    glDeleteTextures(1, &glTextureId);
    ReleaseSharedInteropDevice(interopDev, d3d11Device);
    return 0;
  }

  if (outInteropDevice) *outInteropDevice = interopDev;
  if (outInteropTexture) *outInteropTexture = interopTex;
  return glTextureId;
}

// ============================================================================
// Helper: get ID3D11Device* from ID3D11Texture2D* via vtable
// ID3D11DeviceChild::GetDevice is vtable index 3
// ============================================================================
static void* GetDeviceFromTexture(void* d3d11Texture) {
  if (!d3d11Texture) return nullptr;
  IUnknown* unk = static_cast<IUnknown*>(d3d11Texture);
  auto vtable = *reinterpret_cast<void***>(unk);
  // ID3D11DeviceChild::GetDevice(ID3D11Device** ppDevice) is at index 3
  typedef void(__stdcall * GetDeviceFn)(IUnknown * self, void** ppDevice);
  auto getDevice = reinterpret_cast<GetDeviceFn>(vtable[3]);
  void* device = nullptr;
  getDevice(unk, &device);
  // GetDevice adds a reference, release it to balance
  if (device) {
    static_cast<IUnknown*>(device)->Release();
  }
  return device;
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
      break;
    default:
      return nullptr;
  }

  TextureDescriptor descriptor = {info.width, info.height, pixelFormat, false, 1, usage};
  void* d3d11Tex = hardwareBuffer;  // HardwareBufferRef == void* == ID3D11Texture2D*

  // Try memory_object path first
  if (IsMemoryObjectInteropAvailable()) {
    unsigned memObj = 0;
    unsigned glTextureId = ImportViaMemoryObject(d3d11Tex, info.width, info.height, &memObj);
    if (glTextureId != 0) {
      auto texture = gpu->makeResource<WGLHardwareTexture>(
          descriptor, hardwareBuffer, static_cast<unsigned>(GL_TEXTURE_2D), glTextureId);
      if (needSwizzle) {
        auto state = gpu->state();
        state->bindTexture(texture.get());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
      }
      texture->memoryObject = memObj;
      return texture;
    }
  }

  // Fall back to WGL_NV_DX_interop
  if (IsNVDXInteropAvailable()) {
    void* d3dDevice = GetDeviceFromTexture(d3d11Tex);
    if (!d3dDevice) {
      LOGE("WGLHardwareTexture: Failed to get D3D11 device from texture.");
      return nullptr;
    }
    void* interopDev = nullptr;
    void* interopTex = nullptr;
    unsigned glTextureId = ImportViaWglDX(d3dDevice, d3d11Tex, &interopDev, &interopTex);
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
  // Cleanup wglDX interop
  if (interopTexture) {
    if (wglDXUnlockObjectsNV && interopDevice) {
      wglDXUnlockObjectsNV(interopDevice, 1, reinterpret_cast<HANDLE*>(&interopTexture));
    }
    if (wglDXUnregisterObjectNV && interopDevice) {
      wglDXUnregisterObjectNV(interopDevice, interopTexture);
    }
    interopTexture = nullptr;
  }
  if (interopDevice) {
    ReleaseSharedInteropDevice(interopDevice, d3d11Device);
    interopDevice = nullptr;
  }

  // Delete GL texture via base class
  GLTexture::onReleaseTexture(gpu);

  // Cleanup memory object
  if (memoryObject != 0 && glDeleteMemoryObjectsEXT) {
    glDeleteMemoryObjectsEXT(1, &memoryObject);
    memoryObject = 0;
  }
}

}  // namespace tgfx
