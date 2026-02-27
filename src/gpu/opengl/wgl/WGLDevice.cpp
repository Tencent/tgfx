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

#include "tgfx/gpu/opengl/wgl/WGLDevice.h"
#include "gpu/opengl/wgl/WGLGPU.h"
#include "gpu/opengl/wgl/WGLInterface.h"

namespace tgfx {
static void GetPixelFormatsToTry(HDC deviceContext, int formatsToTry[2]) {
  auto wglInterface = WGLInterface::Get();
  if (!wglInterface->pixelFormatSupport) {
    return;
  }
  std::vector<int> intAttributes{WGL_DRAW_TO_WINDOW,
                                 TRUE,
                                 WGL_DOUBLE_BUFFER,
                                 TRUE,
                                 WGL_ACCELERATION,
                                 WGL_FULL_ACCELERATION,
                                 WGL_SUPPORT_OPENGL,
                                 TRUE,
                                 WGL_COLOR_BITS,
                                 24,
                                 WGL_ALPHA_BITS,
                                 8,
                                 WGL_STENCIL_BITS,
                                 8,
                                 0,
                                 0};

  auto format = formatsToTry[0] ? &formatsToTry[0] : &formatsToTry[1];
  unsigned numFormats = 0;
  constexpr float floatAttributes[] = {0, 0};
  wglInterface->wglChoosePixelFormat(deviceContext, intAttributes.data(), floatAttributes, 1,
                                     format, &numFormats);
}

static HGLRC CreateGLContext(HDC deviceContext, HGLRC sharedContext) {
  auto oldDeviceContext = wglGetCurrentDC();
  auto oldGLContext = wglGetCurrentContext();

  HGLRC glContext = nullptr;
  auto wglInterface = WGLInterface::Get();
  if (wglInterface->createContextAttribsSupport) {
    const int coreProfileAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION,
        wglInterface->glMajorMax,
        WGL_CONTEXT_MINOR_VERSION,
        wglInterface->glMinorMax,
        WGL_CONTEXT_PROFILE_MASK,
        WGL_CONTEXT_CORE_PROFILE_BIT,
        0,
    };
    glContext =
        wglInterface->wglCreateContextAttribs(deviceContext, sharedContext, coreProfileAttribs);
  }

  if (glContext == nullptr) {
    glContext = wglCreateContext(deviceContext);
    if (glContext == nullptr) {
      LOGE("CreateGLContext() wglCreateContext failed.");
      return nullptr;
    }

    if (sharedContext != nullptr && !wglShareLists(sharedContext, glContext)) {
      wglDeleteContext(glContext);
      return nullptr;
    }
  }

  if (wglMakeCurrent(deviceContext, glContext) && wglInterface->swapIntervalSupport) {
    wglInterface->wglSwapInterval(1);
  }

  wglMakeCurrent(oldDeviceContext, oldGLContext);
  return glContext;
}

static HWND CreateParentWindow() {
  static ATOM windowClass = 0;
  const auto hInstance = (HINSTANCE)GetModuleHandle(nullptr);
  if (!windowClass) {
    WNDCLASS wc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hInstance = hInstance;
    wc.lpfnWndProc = static_cast<WNDPROC>(DefWindowProc);
    wc.lpszClassName = TEXT("WC_TGFX");
    wc.lpszMenuName = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

    windowClass = RegisterClass(&wc);
    if (!windowClass) {
      LOGE("CreateParentWindow() register window class failed.");
      return nullptr;
    }
  }

  HWND window = CreateWindow(TEXT("WC_TGFX"), TEXT("INVISIBLE_WINDOW"), WS_OVERLAPPEDWINDOW, 0, 0,
                             1, 1, nullptr, nullptr, hInstance, nullptr);
  if (window == nullptr) {
    LOGE("CreateParentWindow() create window failed.");
    return nullptr;
  }
  return window;
}

static bool CreatePbufferContext(HDC parentDeviceContext, HGLRC sharedContext, HPBUFFER& pBuffer,
                                 HDC& deviceContext, HGLRC& glContext) {
  auto wglInterface = WGLInterface::Get();
  if (!wglInterface->pixelFormatSupport || !wglInterface->pBufferSupport) {
    return false;
  }

  static int pixelFormat = -1;
  static std::once_flag flag;
  std::call_once(flag, [parentDeviceContext] {
    int pixelFormatsToTry[2] = {-1, -1};
    GetPixelFormatsToTry(parentDeviceContext, pixelFormatsToTry);
    pixelFormat = pixelFormatsToTry[0];
  });

  if (pixelFormat == -1) {
    return false;
  }

  pBuffer = wglInterface->wglCreatePbuffer(parentDeviceContext, pixelFormat, 1, 1, nullptr);
  if (pBuffer != nullptr) {
    deviceContext = wglInterface->wglGetPbufferDC(pBuffer);
    if (deviceContext != nullptr) {
      glContext = CreateGLContext(deviceContext, sharedContext);
      if (glContext != nullptr) {
        return true;
      }
      wglInterface->wglReleasePbufferDC(pBuffer, deviceContext);
      deviceContext = nullptr;
    }
    wglInterface->wglDestroyPbuffer(pBuffer);
    pBuffer = nullptr;
  }
  return false;
}

static HGLRC CreateWGLContext(HDC deviceContext, HGLRC sharedContext) {
  auto set = false;
  int pixelFormatsToTry[2] = {-1, -1};
  GetPixelFormatsToTry(deviceContext, pixelFormatsToTry);
  for (auto f = 0; !set && pixelFormatsToTry[f] && f < 2; ++f) {
    PIXELFORMATDESCRIPTOR descriptor;
    DescribePixelFormat(deviceContext, pixelFormatsToTry[f], sizeof(descriptor), &descriptor);
    set = SetPixelFormat(deviceContext, pixelFormatsToTry[f], &descriptor);
  }

  if (!set) {
    return nullptr;
  }
  return CreateGLContext(deviceContext, sharedContext);
}

void* GLDevice::CurrentNativeHandle() {
  return wglGetCurrentContext();
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto glContext = wglGetCurrentContext();
  auto glDevice = GLDevice::Get(glContext);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<WGLDevice>(glDevice);
  }
  auto deviceContext = wglGetCurrentDC();
  return WGLDevice::Wrap(deviceContext, glContext, nullptr, nullptr, nullptr, true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  HWND window = nullptr;
  HDC parentDeviceContext = nullptr;
  HPBUFFER pBuffer = nullptr;
  HDC deviceContext = nullptr;
  HGLRC glContext = nullptr;
  bool result = false;
  do {
    window = CreateParentWindow();
    if (window == nullptr) {
      LOGE("GLDevice::Make() create window failed!");
      break;
    }
    parentDeviceContext = GetDC(window);
    if (parentDeviceContext == nullptr) {
      LOGE("GLDevice::Make() get device context failed!");
      break;
    }
    if (!CreatePbufferContext(parentDeviceContext, static_cast<HGLRC>(sharedContext), pBuffer,
                              deviceContext, glContext)) {
      LOGE("GLDevice::Make() create pbuffer context failed!");
      break;
    }
    result = true;
  } while (false);

  ReleaseDC(window, parentDeviceContext);
  DestroyWindow(window);

  if (!result) {
    return nullptr;
  }
  auto device = WGLDevice::Wrap(deviceContext, glContext, static_cast<HGLRC>(sharedContext),
                                nullptr, pBuffer, false);
  if (device == nullptr) {
    wglDeleteContext(glContext);
    auto wglInterface = WGLInterface::Get();
    wglInterface->wglReleasePbufferDC(pBuffer, deviceContext);
    wglInterface->wglDestroyPbuffer(pBuffer);
  }
  return device;
}

std::shared_ptr<WGLDevice> WGLDevice::MakeFrom(HWND nativeWindow, HGLRC sharedContext) {
  if (nativeWindow == nullptr) {
    return nullptr;
  }
  auto deviceContext = GetDC(nativeWindow);
  auto glContext = CreateWGLContext(deviceContext, sharedContext);
  auto device =
      WGLDevice::Wrap(deviceContext, glContext, sharedContext, nativeWindow, nullptr, false);
  if (device == nullptr) {
    wglDeleteContext(glContext);
    ReleaseDC(nativeWindow, deviceContext);
  }
  return device;
}

std::shared_ptr<WGLDevice> WGLDevice::Wrap(HDC deviceContext, HGLRC glContext, HGLRC sharedContext,
                                           HWND nativeWindow, HPBUFFER pBuffer,
                                           bool externallyOwned) {
  auto glDevice = GLDevice::Get(glContext);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<WGLDevice>(glDevice);
  }
  if (glContext == nullptr) {
    return nullptr;
  }

  auto oldDeviceContext = wglGetCurrentDC();
  auto oldGLContext = wglGetCurrentContext();
  if (oldGLContext != glContext) {
    auto result = wglMakeCurrent(deviceContext, glContext);
    if (!result) {
      return nullptr;
    }
  }
  std::shared_ptr<WGLDevice> device = nullptr;
  auto interface = GLInterface::GetNative();
  if (interface != nullptr) {
    auto gpu = std::make_unique<WGLGPU>(std::move(interface));
    device = std::shared_ptr<WGLDevice>(new WGLDevice(std::move(gpu), glContext));
    device->nativeWindow = nativeWindow;
    device->pBuffer = pBuffer;
    device->externallyOwned = externallyOwned;
    device->deviceContext = deviceContext;
    device->sharedContext = sharedContext;
    device->weakThis = device;
  }
  if (oldGLContext != glContext) {
    wglMakeCurrent(deviceContext, nullptr);
    if (oldDeviceContext) {
      wglMakeCurrent(oldDeviceContext, oldGLContext);
    }
  }
  return device;
}

WGLDevice::WGLDevice(std::unique_ptr<GPU> gpu, HGLRC glContext)
    : GLDevice(std::move(gpu), glContext), glContext(glContext) {
}

WGLDevice::~WGLDevice() {
  releaseAll();
  if (externallyOwned) {
    return;
  }
  wglDeleteContext(glContext);
  if (nativeWindow != nullptr) {
    ReleaseDC(nativeWindow, deviceContext);
  }
  if (pBuffer != nullptr) {
    auto wglInterface = WGLInterface::Get();
    wglInterface->wglReleasePbufferDC(pBuffer, deviceContext);
    wglInterface->wglDestroyPbuffer(pBuffer);
  }
}

bool WGLDevice::sharableWith(void* nativeConext) const {
  return nativeHandle == nativeConext || sharedContext == nativeConext;
}

bool WGLDevice::onLockContext() {
  oldGLContext = wglGetCurrentContext();
  oldDeviceContext = wglGetCurrentDC();
  if (oldGLContext == glContext) {
    return true;
  }
  if (!wglMakeCurrent(deviceContext, glContext)) {
    return false;
  }
  return true;
}

void WGLDevice::onUnlockContext() {
  if (oldGLContext == glContext) {
    return;
  }
  wglMakeCurrent(deviceContext, nullptr);
  if (oldDeviceContext != nullptr) {
    wglMakeCurrent(oldDeviceContext, oldGLContext);
  }
}
}  // namespace tgfx
