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

#include "WGLGPU.h"
#include <cstring>
#include "WGLHardwareTexture.h"
#include "WGLInteropState.h"
#include "tgfx/platform/HardwareBuffer.h"

typedef const char*(WINAPI* PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC hdc);

namespace tgfx {

// ============================================================================
// Extension detection helpers (file-scope, no state captured)
// ============================================================================

namespace {

bool HasExtension(const char* extensions, const char* extension) {
  if (!extensions || !extension) {
    return false;
  }
  const char* start = extensions;
  size_t extLen = strlen(extension);
  const char* where = nullptr;
  while ((where = strstr(start, extension)) != nullptr) {
    const char* terminator = where + extLen;
    if ((where == start || *(where - 1) == ' ') && (*terminator == ' ' || *terminator == '\0')) {
      return true;
    }
    start = terminator;
  }
  return false;
}

bool CheckNVDXInteropFunctions(WGLInteropState* state) {
  state->wglDXOpenDeviceNV =
      reinterpret_cast<PFNWGLDXOPENDEVICENVPROC>(wglGetProcAddress("wglDXOpenDeviceNV"));
  state->wglDXCloseDeviceNV =
      reinterpret_cast<PFNWGLDXCLOSEDEVICENVPROC>(wglGetProcAddress("wglDXCloseDeviceNV"));
  state->wglDXRegisterObjectNV =
      reinterpret_cast<PFNWGLDXREGISTEROBJECTNVPROC>(wglGetProcAddress("wglDXRegisterObjectNV"));
  state->wglDXUnregisterObjectNV = reinterpret_cast<PFNWGLDXUNREGISTEROBJECTNVPROC>(
      wglGetProcAddress("wglDXUnregisterObjectNV"));
  state->wglDXLockObjectsNV =
      reinterpret_cast<PFNWGLDXLOCKOBJECTSNVPROC>(wglGetProcAddress("wglDXLockObjectsNV"));
  state->wglDXUnlockObjectsNV =
      reinterpret_cast<PFNWGLDXUNLOCKOBJECTSNVPROC>(wglGetProcAddress("wglDXUnlockObjectsNV"));
  return state->wglDXOpenDeviceNV && state->wglDXCloseDeviceNV && state->wglDXRegisterObjectNV &&
         state->wglDXUnregisterObjectNV && state->wglDXLockObjectsNV && state->wglDXUnlockObjectsNV;
}

bool CheckNVDXInteropViaCurrentContext(WGLInteropState* state) {
  auto wglGetExtensionsStringARB = reinterpret_cast<PFNWGLGETEXTENSIONSSTRINGARBPROC>(
      wglGetProcAddress("wglGetExtensionsStringARB"));
  if (!wglGetExtensionsStringARB) {
    return false;
  }
  HDC dc = wglGetCurrentDC();
  const char* extensions = wglGetExtensionsStringARB(dc);
  if (!HasExtension(extensions, "WGL_NV_DX_interop")) {
    return false;
  }
  return CheckNVDXInteropFunctions(state);
}

bool CheckNVDXInteropViaTempContext(WGLInteropState* state) {
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
  auto wglGetExtensionsStringARB = reinterpret_cast<PFNWGLGETEXTENSIONSSTRINGARBPROC>(
      wglGetProcAddress("wglGetExtensionsStringARB"));
  bool hasExt = false;
  if (wglGetExtensionsStringARB) {
    const char* extensions = wglGetExtensionsStringARB(tempDC);
    hasExt = HasExtension(extensions, "WGL_NV_DX_interop");
  }
  bool result = hasExt && CheckNVDXInteropFunctions(state);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(tempContext);
  ReleaseDC(tempHwnd, tempDC);
  DestroyWindow(tempHwnd);
  return result;
}

bool CheckMemoryObjectInteropFunctions(WGLInteropState* state) {
  state->glCreateMemoryObjectsEXT = reinterpret_cast<PFNGLCREATEMEMORYOBJECTSEXTPROC>(
      wglGetProcAddress("glCreateMemoryObjectsEXT"));
  state->glDeleteMemoryObjectsEXT = reinterpret_cast<PFNGLDELETEMEMORYOBJECTSEXTPROC>(
      wglGetProcAddress("glDeleteMemoryObjectsEXT"));
  state->glMemoryObjectParameterivEXT = reinterpret_cast<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC>(
      wglGetProcAddress("glMemoryObjectParameterivEXT"));
  state->glTexStorageMem2DEXT =
      reinterpret_cast<PFNGLTEXSTORAGEMEM2DEXTPROC>(wglGetProcAddress("glTexStorageMem2DEXT"));
  state->glImportMemoryWin32HandleEXT = reinterpret_cast<PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC>(
      wglGetProcAddress("glImportMemoryWin32HandleEXT"));
  return state->glCreateMemoryObjectsEXT && state->glDeleteMemoryObjectsEXT &&
         state->glMemoryObjectParameterivEXT && state->glTexStorageMem2DEXT &&
         state->glImportMemoryWin32HandleEXT;
}

}  // namespace

// ============================================================================
// WGLGPU
// ============================================================================

WGLGPU::WGLGPU(std::shared_ptr<GLInterface> glInterface)
    : GLGPU(std::move(glInterface)), interopState(std::make_unique<WGLInteropState>()) {
}

WGLGPU::~WGLGPU() {
  for (auto& shared : interopState->sharedDevices) {
    if (shared.interopDevice && interopState->wglDXCloseDeviceNV) {
      interopState->wglDXCloseDeviceNV(shared.interopDevice);
    }
  }
  interopState->sharedDevices.clear();
}

bool HardwareBufferAvailable() {
  return true;
}

bool WGLGPU::isNVDXInteropAvailable() {
  if (interopState->nvChecked) {
    return interopState->nvAvailable;
  }
  interopState->nvChecked = true;
  HGLRC currentContext = wglGetCurrentContext();
  interopState->nvAvailable = currentContext ? CheckNVDXInteropViaCurrentContext(interopState.get())
                                             : CheckNVDXInteropViaTempContext(interopState.get());
  return interopState->nvAvailable;
}

bool WGLGPU::isMemoryObjectInteropAvailable() {
  if (interopState->memObjChecked) {
    return interopState->memObjAvailable;
  }
  if (!wglGetCurrentContext()) {
    return false;
  }
  interopState->memObjChecked = true;

  typedef const unsigned char*(GL_FUNCTION_TYPE * PFNGLGETSTRINGIPROC)(unsigned name,
                                                                       unsigned index);
  auto glGetStringi = reinterpret_cast<PFNGLGETSTRINGIPROC>(wglGetProcAddress("glGetStringi"));
  if (!glGetStringi) {
    interopState->memObjAvailable = false;
    return false;
  }
  int numExtensions = 0;
  glGetIntegerv(0x821D, &numExtensions);  // GL_NUM_EXTENSIONS
  bool hasMemObj = false;
  bool hasMemObjWin32 = false;
  for (int i = 0; i < numExtensions; i++) {
    const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
    if (ext) {
      if (strcmp(ext, "GL_EXT_memory_object") == 0) {
        hasMemObj = true;
      } else if (strcmp(ext, "GL_EXT_memory_object_win32") == 0) {
        hasMemObjWin32 = true;
      }
    }
    if (hasMemObj && hasMemObjWin32) {
      break;
    }
  }
  if (!hasMemObj || !hasMemObjWin32) {
    interopState->memObjAvailable = false;
    return false;
  }
  interopState->memObjAvailable = CheckMemoryObjectInteropFunctions(interopState.get());
  return interopState->memObjAvailable;
}

HANDLE WGLGPU::acquireSharedInteropDevice(ID3D11Device* d3d11Device) {
  if (!interopState->wglDXOpenDeviceNV || !d3d11Device) {
    return nullptr;
  }
  for (auto& shared : interopState->sharedDevices) {
    if (shared.d3d11Device == d3d11Device && shared.interopDevice != nullptr) {
      shared.refCount++;
      return shared.interopDevice;
    }
  }
  HANDLE interopDev = interopState->wglDXOpenDeviceNV(d3d11Device);
  if (!interopDev) {
    return nullptr;
  }
  interopState->sharedDevices.push_back({interopDev, d3d11Device, 1});
  return interopDev;
}

void WGLGPU::releaseSharedInteropDevice(HANDLE interopDevice, ID3D11Device* d3d11Device) {
  if (!interopDevice) {
    return;
  }
  for (auto it = interopState->sharedDevices.begin(); it != interopState->sharedDevices.end();
       ++it) {
    if (it->interopDevice == interopDevice && it->d3d11Device == d3d11Device) {
      it->refCount--;
      if (it->refCount <= 0) {
        if (interopState->wglDXCloseDeviceNV) {
          interopState->wglDXCloseDeviceNV(interopDevice);
        }
        interopState->sharedDevices.erase(it);
      }
      return;
    }
  }
  // Not found in the pool — close directly as a fallback.
  if (interopState->wglDXCloseDeviceNV) {
    interopState->wglDXCloseDeviceNV(interopDevice);
  }
}

std::vector<std::shared_ptr<Texture>> WGLGPU::importHardwareTextures(
    HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.format == HardwareBufferFormat::YCBCR_420_SP) {
    // NV12 textures should be converted to BGRA before reaching here.
    return {};
  }
  auto texture = WGLHardwareTexture::MakeFrom(this, hardwareBuffer, usage);
  if (texture == nullptr) {
    return {};
  }
  return {std::move(texture)};
}

}  // namespace tgfx
