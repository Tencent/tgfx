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

#ifdef _WIN32

#include "D3D11VideoStream.h"
#include <windows.h>
#include <cstring>
#include <vector>
#include <GL/gl.h>

#include "core/utils/Log.h"
#include "tgfx/core/Clock.h"

#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLTexture.h"
#include "gpu/resources/DefaultTextureView.h"

// WGL_NV_DX_interop definitions
#define WGL_ACCESS_READ_ONLY_NV 0x0000
#define WGL_ACCESS_READ_WRITE_NV 0x0001
#define WGL_ACCESS_WRITE_DISCARD_NV 0x0002

// GL_EXT_memory_object / GL_EXT_memory_object_win32 definitions
#define GL_TEXTURE_TILING_EXT 0x9580
#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#define GL_PROTECTED_MEMORY_OBJECT_EXT 0x959B
#define GL_NUM_TILING_TYPES_EXT 0x9582
#define GL_TILING_TYPES_EXT 0x9583
#define GL_OPTIMAL_TILING_EXT 0x9584
#define GL_LINEAR_TILING_EXT 0x9585
#define GL_HANDLE_TYPE_OPAQUE_WIN32_EXT 0x9587
#define GL_HANDLE_TYPE_D3D11_IMAGE_EXT 0x958B
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

typedef BOOL(WINAPI* PFNWGLDXSETRESOURCESHAREHANDLENVPROC)(void* dxObject, HANDLE shareHandle);
typedef HANDLE(WINAPI* PFNWGLDXOPENDEVICENVPROC)(void* dxDevice);
typedef BOOL(WINAPI* PFNWGLDXCLOSEDEVICENVPROC)(HANDLE hDevice);
typedef HANDLE(WINAPI* PFNWGLDXREGISTEROBJECTNVPROC)(HANDLE hDevice, void* dxObject, GLuint name,
                                                     GLenum type, GLenum access);
typedef BOOL(WINAPI* PFNWGLDXUNREGISTEROBJECTNVPROC)(HANDLE hDevice, HANDLE hObject);
typedef BOOL(WINAPI* PFNWGLDXOBJECTACCESSNVPROC)(HANDLE hObject, GLenum access);
typedef BOOL(WINAPI* PFNWGLDXLOCKOBJECTSNVPROC)(HANDLE hDevice, GLint count, HANDLE* hObjects);
typedef BOOL(WINAPI* PFNWGLDXUNLOCKOBJECTSNVPROC)(HANDLE hDevice, GLint count, HANDLE* hObjects);

// WGL extension string function typedef
typedef const char*(WINAPI* PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC hdc);

namespace tgfx {

// WGL_NV_DX_interop function pointers (file-local)
static PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV = nullptr;
static PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV = nullptr;
static PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV = nullptr;
static PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV = nullptr;
static PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV = nullptr;
static PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV = nullptr;

static std::mutex g_nvInteropMutex;
static bool g_nvInteropChecked = false;
static bool g_nvInteropAvailable = false;

// GL_EXT_memory_object function pointers
typedef void(GL_FUNCTION_TYPE* PFNGLCREATEMEMORYOBJECTSEXTPROC)(int n, unsigned* memoryObjects);
typedef void(GL_FUNCTION_TYPE* PFNGLDELETEMEMORYOBJECTSEXTPROC)(int n, const unsigned* memoryObjects);
typedef unsigned char(GL_FUNCTION_TYPE* PFNGLISMEMORYOBJECTEXTPROC)(unsigned memoryObject);
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

static PFNGLCREATEMEMORYOBJECTSEXTPROC glCreateMemoryObjectsEXT = nullptr;
static PFNGLDELETEMEMORYOBJECTSEXTPROC glDeleteMemoryObjectsEXT = nullptr;
static PFNGLMEMORYOBJECTPARAMETERIVEXTPROC glMemoryObjectParameterivEXT = nullptr;
static PFNGLTEXSTORAGEMEM2DEXTPROC glTexStorageMem2DEXT = nullptr;
static PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC glImportMemoryWin32HandleEXT = nullptr;

static std::mutex g_memObjInteropMutex;
static bool g_memObjInteropChecked = false;
static bool g_memObjInteropAvailable = false;

// Shared interop device management
// Each D3D11Device can only create ~15 interop devices, so we share one per D3D11Device
struct SharedInteropDevice {
  void* interopDevice = nullptr;
  ID3D11Device* d3d11Device = nullptr;  // Raw pointer for comparison
  int refCount = 0;
};

static std::mutex g_sharedInteropMutex;
static std::vector<SharedInteropDevice> g_sharedInteropDevices;

// Get or create shared interop device for the given D3D11 device
static void* AcquireSharedInteropDevice(ID3D11Device* d3d11Device) {
  if (!wglDXOpenDeviceNV || !d3d11Device) {
    return nullptr;
  }
  
  std::lock_guard<std::mutex> lock(g_sharedInteropMutex);
  
  // Find existing shared interop device for this D3D11 device
  for (auto& shared : g_sharedInteropDevices) {
    if (shared.d3d11Device == d3d11Device && shared.interopDevice != nullptr) {
      shared.refCount++;
      return shared.interopDevice;
    }
  }
  
  // Create new interop device for this D3D11 device
  void* interopDevice = wglDXOpenDeviceNV(d3d11Device);
  if (!interopDevice) {
    return nullptr;
  }
  
  // Add to shared list
  SharedInteropDevice shared;
  shared.interopDevice = interopDevice;
  shared.d3d11Device = d3d11Device;
  shared.refCount = 1;
  g_sharedInteropDevices.push_back(shared);
  
  LOGI("D3D11VideoStream: Created shared interop device for D3D11Device %p", d3d11Device);
  return interopDevice;
}

// Release reference to shared interop device
static void ReleaseSharedInteropDevice(void* interopDevice, ID3D11Device* d3d11Device) {
  if (!interopDevice) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(g_sharedInteropMutex);
  
  for (auto it = g_sharedInteropDevices.begin(); it != g_sharedInteropDevices.end(); ++it) {
    if (it->interopDevice == interopDevice && it->d3d11Device == d3d11Device) {
      it->refCount--;
      if (it->refCount <= 0) {
        // Last reference, close the interop device
        if (wglDXCloseDeviceNV) {
          wglDXCloseDeviceNV(interopDevice);
        }
        LOGI("D3D11VideoStream: Closed shared interop device for D3D11Device %p", d3d11Device);
        g_sharedInteropDevices.erase(it);
      }
      return;
    }
  }
  
  // Fallback: if not found in shared list, close directly
  if (wglDXCloseDeviceNV) {
    wglDXCloseDeviceNV(interopDevice);
  }
}

// ============================================================================
// Shared VideoProcessor management
// One VideoProcessor per (D3D11Device, width, height) combination
// ============================================================================
struct SharedVideoProcessorKey {
  ID3D11Device* device;
  int width;
  int height;
  
  bool operator==(const SharedVideoProcessorKey& other) const {
    return device == other.device && width == other.width && height == other.height;
  }
};

struct SharedVideoProcessor {
  SharedVideoProcessorKey key;
  ComPtr<ID3D11VideoDevice> videoDevice;
  ComPtr<ID3D11VideoContext> videoContext;
  ComPtr<ID3D11VideoProcessor> videoProcessor;
  ComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnum;
  int refCount = 0;
  bool colorSpaceConfigured = false;
};

static std::mutex g_sharedVideoProcessorMutex;
static std::vector<SharedVideoProcessor> g_sharedVideoProcessors;

// Acquire shared VideoProcessor for the given device and dimensions
static SharedVideoProcessor* AcquireSharedVideoProcessor(ID3D11Device* device,
                                                          ID3D11DeviceContext* context,
                                                          int width, int height) {
  if (!device || !context || width <= 0 || height <= 0) {
    return nullptr;
  }
  
  std::lock_guard<std::mutex> lock(g_sharedVideoProcessorMutex);
  
  SharedVideoProcessorKey key = {device, width, height};
  
  // Find existing shared VideoProcessor
  for (auto& shared : g_sharedVideoProcessors) {
    if (shared.key == key) {
      shared.refCount++;
      return &shared;
    }
  }
  
  // Create new VideoProcessor
  SharedVideoProcessor newShared;
  newShared.key = key;
  newShared.refCount = 1;
  
  // Get video device interface
  HRESULT hr = device->QueryInterface(IID_PPV_ARGS(newShared.videoDevice.GetAddressOf()));
  if (FAILED(hr)) {
    LOGE("SharedVideoProcessor: Failed to get ID3D11VideoDevice, hr=0x%08X", hr);
    return nullptr;
  }
  
  // Get video context interface
  hr = context->QueryInterface(IID_PPV_ARGS(newShared.videoContext.GetAddressOf()));
  if (FAILED(hr)) {
    LOGE("SharedVideoProcessor: Failed to get ID3D11VideoContext, hr=0x%08X", hr);
    return nullptr;
  }
  
  // Create video processor enumerator
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
  contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  contentDesc.InputWidth = static_cast<UINT>(width);
  contentDesc.InputHeight = static_cast<UINT>(height);
  contentDesc.OutputWidth = static_cast<UINT>(width);
  contentDesc.OutputHeight = static_cast<UINT>(height);
  contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  
  hr = newShared.videoDevice->CreateVideoProcessorEnumerator(&contentDesc,
                                                              newShared.videoProcessorEnum.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("SharedVideoProcessor: Failed to create enumerator, hr=0x%08X", hr);
    return nullptr;
  }
  
  // Create video processor
  hr = newShared.videoDevice->CreateVideoProcessor(newShared.videoProcessorEnum.Get(), 0,
                                                    newShared.videoProcessor.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("SharedVideoProcessor: Failed to create processor, hr=0x%08X", hr);
    return nullptr;
  }
  
  // Configure color spaces (only once)
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColorSpace = {};
  inputColorSpace.Usage = 0;           // Playback
  inputColorSpace.RGB_Range = 1;       // Limited range (16-235)
  inputColorSpace.YCbCr_Matrix = 1;    // BT.709
  inputColorSpace.YCbCr_xvYCC = 0;
  inputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  newShared.videoContext->VideoProcessorSetStreamColorSpace(newShared.videoProcessor.Get(), 0, &inputColorSpace);
  
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColorSpace = {};
  outputColorSpace.Usage = 0;          // Playback
  outputColorSpace.RGB_Range = 0;      // Full range (0-255)
  outputColorSpace.YCbCr_Matrix = 1;   // BT.709
  outputColorSpace.YCbCr_xvYCC = 0;
  outputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
  newShared.videoContext->VideoProcessorSetOutputColorSpace(newShared.videoProcessor.Get(), &outputColorSpace);
  newShared.colorSpaceConfigured = true;
  
  g_sharedVideoProcessors.push_back(std::move(newShared));
  LOGI("SharedVideoProcessor: Created for device=%p, %dx%d (total=%zu)",
       device, width, height, g_sharedVideoProcessors.size());
  
  return &g_sharedVideoProcessors.back();
}

// Release reference to shared VideoProcessor
static void ReleaseSharedVideoProcessor(ID3D11Device* device, int width, int height) {
  std::lock_guard<std::mutex> lock(g_sharedVideoProcessorMutex);
  
  SharedVideoProcessorKey key = {device, width, height};
  
  for (auto it = g_sharedVideoProcessors.begin(); it != g_sharedVideoProcessors.end(); ++it) {
    if (it->key == key) {
      it->refCount--;
      if (it->refCount <= 0) {
        LOGI("SharedVideoProcessor: Released for device=%p, %dx%d (remaining=%zu)",
             device, width, height, g_sharedVideoProcessors.size() - 1);
        g_sharedVideoProcessors.erase(it);
      }
      return;
    }
  }
}

// Forward declarations for static helper functions
static bool HasExtension(const char* extensions, const char* extension);
static bool CheckNVDXInteropWithContext();
static bool CheckNVDXInteropWithTempContext();

// Helper function to check if an extension is in the extension string
static bool HasExtension(const char* extensions, const char* extension) {
  if (extensions == nullptr || extension == nullptr) {
    return false;
  }
  const char* start = extensions;
  const char* where;
  const char* terminator;
  size_t extLen = strlen(extension);

  while ((where = strstr(start, extension)) != nullptr) {
    terminator = where + extLen;
    if (where == start || *(where - 1) == ' ') {
      if (*terminator == ' ' || *terminator == '\0') {
        return true;
      }
    }
    start = terminator;
  }
  return false;
}

// Check extension availability with existing context
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

// Check extension availability by creating a temporary context
static bool CheckNVDXInteropWithTempContext() {
  // Create a temporary window class
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc = DefWindowProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = TEXT("TGFXWGLTempWindow");

  if (!RegisterClassEx(&wc)) {
    DWORD error = GetLastError();
    if (error != ERROR_CLASS_ALREADY_EXISTS) {
      LOGE("IsNVDXInteropAvailable: Failed to register window class, error=%lu", error);
      return false;
    }
  }

  // Create a temporary hidden window
  HWND tempHwnd = CreateWindowEx(0, TEXT("TGFXWGLTempWindow"), TEXT("TGFX WGL Temp"),
                                 WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr,
                                 GetModuleHandle(nullptr), nullptr);

  if (tempHwnd == nullptr) {
    LOGE("IsNVDXInteropAvailable: Failed to create temp window");
    return false;
  }

  HDC tempDC = GetDC(tempHwnd);
  if (tempDC == nullptr) {
    DestroyWindow(tempHwnd);
    LOGE("IsNVDXInteropAvailable: Failed to get DC");
    return false;
  }

  // Set up a simple pixel format to create initial context
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
  if (tempFormat == 0) {
    ReleaseDC(tempHwnd, tempDC);
    DestroyWindow(tempHwnd);
    LOGE("IsNVDXInteropAvailable: ChoosePixelFormat failed");
    return false;
  }

  if (!SetPixelFormat(tempDC, tempFormat, &tempPfd)) {
    ReleaseDC(tempHwnd, tempDC);
    DestroyWindow(tempHwnd);
    LOGE("IsNVDXInteropAvailable: SetPixelFormat failed");
    return false;
  }

  // Create a temporary OpenGL context
  HGLRC tempContext = wglCreateContext(tempDC);
  if (tempContext == nullptr) {
    ReleaseDC(tempHwnd, tempDC);
    DestroyWindow(tempHwnd);
    LOGE("IsNVDXInteropAvailable: wglCreateContext failed");
    return false;
  }

  if (!wglMakeCurrent(tempDC, tempContext)) {
    wglDeleteContext(tempContext);
    ReleaseDC(tempHwnd, tempDC);
    DestroyWindow(tempHwnd);
    LOGE("IsNVDXInteropAvailable: wglMakeCurrent failed");
    return false;
  }

  // Get the WGL extension string
  auto wglGetExtensionsStringARB =
      (PFNWGLGETEXTENSIONSSTRINGARBPROC)wglGetProcAddress("wglGetExtensionsStringARB");

  bool hasExtension = false;
  if (wglGetExtensionsStringARB != nullptr) {
    const char* extensions = wglGetExtensionsStringARB(tempDC);
    if (extensions != nullptr) {
      hasExtension = HasExtension(extensions, "WGL_NV_DX_interop");
    }
  }

  bool result = false;
  if (hasExtension) {
    // Load function pointers
    result = CheckNVDXInteropWithContext();
  }

  // Cleanup temporary context
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(tempContext);
  ReleaseDC(tempHwnd, tempDC);
  DestroyWindow(tempHwnd);

  return result;
}

bool IsNVDXInteropAvailable() {
  std::lock_guard<std::mutex> lock(g_nvInteropMutex);

  if (g_nvInteropChecked) {
    return g_nvInteropAvailable;
  }
  g_nvInteropChecked = true;

  HGLRC currentContext = wglGetCurrentContext();
  if (currentContext != nullptr) {
    g_nvInteropAvailable = CheckNVDXInteropWithContext();
  } else {
    g_nvInteropAvailable = CheckNVDXInteropWithTempContext();
  }

  return g_nvInteropAvailable;
}

// Check GL_EXT_memory_object / GL_EXT_memory_object_win32 availability and load function pointers
static bool CheckMemoryObjectInteropWithContext() {
  glCreateMemoryObjectsEXT =
      (PFNGLCREATEMEMORYOBJECTSEXTPROC)wglGetProcAddress("glCreateMemoryObjectsEXT");
  glDeleteMemoryObjectsEXT =
      (PFNGLDELETEMEMORYOBJECTSEXTPROC)wglGetProcAddress("glDeleteMemoryObjectsEXT");
  glMemoryObjectParameterivEXT =
      (PFNGLMEMORYOBJECTPARAMETERIVEXTPROC)wglGetProcAddress("glMemoryObjectParameterivEXT");
  glTexStorageMem2DEXT =
      (PFNGLTEXSTORAGEMEM2DEXTPROC)wglGetProcAddress("glTexStorageMem2DEXT");
  glImportMemoryWin32HandleEXT =
      (PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)wglGetProcAddress("glImportMemoryWin32HandleEXT");

  return glCreateMemoryObjectsEXT && glDeleteMemoryObjectsEXT &&
         glMemoryObjectParameterivEXT && glTexStorageMem2DEXT && glImportMemoryWin32HandleEXT;
}

bool IsMemoryObjectInteropAvailable() {
  std::lock_guard<std::mutex> lock(g_memObjInteropMutex);

  if (g_memObjInteropChecked) {
    return g_memObjInteropAvailable;
  }

  HGLRC currentContext = wglGetCurrentContext();
  if (currentContext == nullptr) {
    // No GL context available — don't mark as checked so we can retry later
    return false;
  }
  g_memObjInteropChecked = true;

  // Check for GL_EXT_memory_object and GL_EXT_memory_object_win32 via glGetStringi
  typedef const unsigned char*(GL_FUNCTION_TYPE* PFNGLGETSTRINGIPROC)(unsigned name, unsigned index);
  auto glGetStringi = (PFNGLGETSTRINGIPROC)wglGetProcAddress("glGetStringi");
  if (!glGetStringi) {
    g_memObjInteropAvailable = false;
    return false;
  }

  // GL_NUM_EXTENSIONS = 0x821D
  int numExtensions = 0;
  glGetIntegerv(0x821D, &numExtensions);

  bool hasMemoryObject = false;
  bool hasMemoryObjectWin32 = false;
  for (int i = 0; i < numExtensions; i++) {
    const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
    if (ext) {
      if (strcmp(ext, "GL_EXT_memory_object") == 0) {
        hasMemoryObject = true;
      } else if (strcmp(ext, "GL_EXT_memory_object_win32") == 0) {
        hasMemoryObjectWin32 = true;
      }
    }
    if (hasMemoryObject && hasMemoryObjectWin32) {
      break;
    }
  }

  if (!hasMemoryObject || !hasMemoryObjectWin32) {
    LOGI("D3D11VideoStream: GL_EXT_memory_object=%d, GL_EXT_memory_object_win32=%d",
         hasMemoryObject, hasMemoryObjectWin32);
    g_memObjInteropAvailable = false;
    return false;
  }

  g_memObjInteropAvailable = CheckMemoryObjectInteropWithContext();
  LOGI("D3D11VideoStream: GL_EXT_memory_object interop available=%d", g_memObjInteropAvailable);
  return g_memObjInteropAvailable;
}

std::shared_ptr<D3D11VideoStream> D3D11VideoStream::Make(int width, int height, ID3D11Device* device,
                                                          ID3D11DeviceContext* context) {
  if (width <= 0 || height <= 0 || !device || !context) {
    return nullptr;
  }

  // Check if any D3D11-OpenGL texture sharing path is available.
  // IsNVDXInteropAvailable() uses a temp context fallback so it works without a GL context.
  // IsMemoryObjectInteropAvailable() requires a real GL context — may return false now but
  // will be retried lazily in initGLInterop().
  if (!IsNVDXInteropAvailable() && !IsMemoryObjectInteropAvailable()) {
    return nullptr;
  }

  auto stream =
      std::shared_ptr<D3D11VideoStream>(new D3D11VideoStream(width, height, device, context));
  return stream;
}

D3D11VideoStream::D3D11VideoStream(int width, int height, ID3D11Device* device,
                                   ID3D11DeviceContext* context)
    : ImageStream(width, height) {
  _colorSpace = ColorSpace::SRGB();
  d3d11Device = device;
  d3d11Context = context;
}

D3D11VideoStream::~D3D11VideoStream() {
  releaseGLInterop();
  inputViewCache.clear();
  outputView.Reset();
  
  // Release shared VideoProcessor reference
  if (sharedVideoProcessor) {
    ReleaseSharedVideoProcessor(d3d11Device.Get(), width(), height());
    sharedVideoProcessor = nullptr;
  }
  
  sharedTexture.Reset();
  d3d11Context.Reset();
  d3d11Device.Reset();
}

bool D3D11VideoStream::ensureInitialized() {
  // Already initialized or failed
  if (initialized) {
    return true;
  }
  if (initializationFailed) {
    return false;
  }
  
  // Perform lazy initialization
  if (!initSharedTexture()) {
    initializationFailed = true;
    return false;
  }
  if (!initVideoProcessor()) {
    initializationFailed = true;
    return false;
  }
  
  initialized = true;
  return true;
}

bool D3D11VideoStream::initSharedTexture() {
  if (!d3d11Device) {
    LOGE("D3D11VideoStream: D3D11 device is null.");
    return false;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(width());
  desc.Height = static_cast<UINT>(height());
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  HRESULT hr = d3d11Device->CreateTexture2D(&desc, nullptr, sharedTexture.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create shared texture, hr=0x%08X", hr);
    return false;
  }

  return true;
}

bool D3D11VideoStream::initVideoProcessor() {
  sharedVideoProcessor = AcquireSharedVideoProcessor(d3d11Device.Get(), d3d11Context.Get(),
                                                      width(), height());
  if (!sharedVideoProcessor) {
    LOGE("D3D11VideoStream: Failed to acquire shared video processor.");
    return false;
  }

  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {};
  outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  outputViewDesc.Texture2D.MipSlice = 0;

  HRESULT hr = sharedVideoProcessor->videoDevice->CreateVideoProcessorOutputView(
      sharedTexture.Get(), sharedVideoProcessor->videoProcessorEnum.Get(),
      &outputViewDesc, outputView.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create video processor output view, hr=0x%08X", hr);
    return false;
  }

  return true;
}

ID3D11VideoProcessorInputView* D3D11VideoStream::getOrCreateInputView(ID3D11Texture2D* texture,
                                                                       int subresourceIndex) {
  if (!sharedVideoProcessor) {
    return nullptr;
  }
  
  // If texture changed (different video), clear the cache
  if (cachedInputTexture != texture) {
    inputViewCache.clear();
    cachedInputTexture = texture;
  }

  // Check if we have a cached input view for this subresource index
  auto it = inputViewCache.find(subresourceIndex);
  if (it != inputViewCache.end()) {
    return it->second.Get();
  }

  // Need to create a new input view for this subresource
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
  inputViewDesc.FourCC = 0;
  inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  inputViewDesc.Texture2D.MipSlice = 0;
  inputViewDesc.Texture2D.ArraySlice = static_cast<UINT>(subresourceIndex);

  ComPtr<ID3D11VideoProcessorInputView> inputView;
  HRESULT hr = sharedVideoProcessor->videoDevice->CreateVideoProcessorInputView(
      texture, sharedVideoProcessor->videoProcessorEnum.Get(),
      &inputViewDesc, inputView.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create input view for subresource %d, hr=0x%08X",
         subresourceIndex, hr);
    return nullptr;
  }

  // Cache the input view for this subresource index
  inputViewCache[subresourceIndex] = inputView;

  return inputView.Get();
}

bool D3D11VideoStream::updateTexture(ID3D11Texture2D* decodedTexture, int subresourceIndex,
                                      int srcWidth, int srcHeight) {
  std::lock_guard<std::mutex> lock(locker);

  if (!decodedTexture) {
    return false;
  }
  
  // Lazy initialization - create D3D11 resources on first use
  if (!ensureInitialized()) {
    return false;
  }

  if (!sharedTexture || !sharedVideoProcessor || !outputView) {
    return false;
  }

  // Use provided dimensions or fall back to stream dimensions
  int actualWidth = (srcWidth > 0) ? srcWidth : width();
  int actualHeight = (srcHeight > 0) ? srcHeight : height();

  // Get or create cached input view
  ID3D11VideoProcessorInputView* inputView = getOrCreateInputView(decodedTexture, subresourceIndex);
  if (!inputView) {
    return false;
  }

  // Color spaces are pre-configured in shared VideoProcessor, no need to set every frame
  auto& videoContext = sharedVideoProcessor->videoContext;
  auto& videoProcessor = sharedVideoProcessor->videoProcessor;

  // Set source rectangle only if changed (avoids implicit GPU flush in D3D11 driver)
  RECT srcRect = {0, 0, static_cast<LONG>(actualWidth), static_cast<LONG>(actualHeight)};
  if (srcRect.left != cachedSrcRect.left || srcRect.top != cachedSrcRect.top ||
      srcRect.right != cachedSrcRect.right || srcRect.bottom != cachedSrcRect.bottom) {
    videoContext->VideoProcessorSetStreamSourceRect(videoProcessor.Get(), 0, TRUE, &srcRect);
    cachedSrcRect = srcRect;
  }

  // Set destination rectangle only if changed
  RECT dstRect = {0, 0, static_cast<LONG>(width()), static_cast<LONG>(height())};
  if (dstRect.left != cachedDstRect.left || dstRect.top != cachedDstRect.top ||
      dstRect.right != cachedDstRect.right || dstRect.bottom != cachedDstRect.bottom) {
    videoContext->VideoProcessorSetStreamDestRect(videoProcessor.Get(), 0, TRUE, &dstRect);
    cachedDstRect = dstRect;
  }

  // Set up the stream data
  D3D11_VIDEO_PROCESSOR_STREAM stream = {};
  stream.Enable = TRUE;
  stream.OutputIndex = 0;
  stream.InputFrameOrField = 0;
  stream.PastFrames = 0;
  stream.FutureFrames = 0;
  stream.ppPastSurfaces = nullptr;
  stream.pInputSurface = inputView;
  stream.ppFutureSurfaces = nullptr;

  // Process: convert NV12 to BGRA
  HRESULT hr = videoContext->VideoProcessorBlt(videoProcessor.Get(), outputView.Get(), 0, 1, &stream);
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: VideoProcessorBlt failed, hr=0x%08X", hr);
    return false;
  }

  hasPendingUpdate = true;
  return true;
}

bool D3D11VideoStream::initGLInterop() {
  // Lazy detection of memory_object path (requires GL context, which exists here).
  // Only attempt once — if it fails, fall back to wglDX permanently.
  if (!interopInitialized && !useMemoryObject && !memObjDetectionDone) {
    memObjDetectionDone = true;
    if (IsMemoryObjectInteropAvailable()) {
      useMemoryObject = true;
      LOGI("D3D11VideoStream: Using GL_EXT_memory_object (KMT handle) path");
    }
  }

  if (useMemoryObject) {
    return initGLInteropMemoryObject();
  }

  if (interopInitialized) {
    return glInteropTexture != nullptr;
  }
  interopInitialized = true;

  if (!IsNVDXInteropAvailable() || !d3d11Device || !sharedTexture) {
    return false;
  }

  glInteropDevice = AcquireSharedInteropDevice(d3d11Device.Get());

  if (!glInteropDevice) {
    LOGE("D3D11VideoStream: Failed to get shared GL interop device.");
    return false;
  }

  glGenTextures(1, &glTextureId);
  if (glTextureId == 0) {
    LOGE("D3D11VideoStream: Failed to create OpenGL texture.");
    ReleaseSharedInteropDevice(glInteropDevice, d3d11Device.Get());
    glInteropDevice = nullptr;
    return false;
  }

  glInteropTexture = wglDXRegisterObjectNV(glInteropDevice, sharedTexture.Get(),
                                            glTextureId, GL_TEXTURE_2D,
                                            WGL_ACCESS_READ_ONLY_NV);
  if (!glInteropTexture) {
    LOGE("D3D11VideoStream: Failed to register D3D11 texture with OpenGL.");
    glDeleteTextures(1, &glTextureId);
    glTextureId = 0;
    ReleaseSharedInteropDevice(glInteropDevice, d3d11Device.Get());
    glInteropDevice = nullptr;
    return false;
  }

  return true;
}

bool D3D11VideoStream::initGLInteropMemoryObject() {
  if (interopInitialized) {
    return glTextureId != 0;
  }
  interopInitialized = true;

  if (!glCreateMemoryObjectsEXT || !glImportMemoryWin32HandleEXT || !glTexStorageMem2DEXT) {
    LOGE("D3D11VideoStream: memory_object function pointers not loaded.");
    return false;
  }

  // Get KMT shared handle from sharedTexture
  HANDLE kmtHandle = nullptr;
  ComPtr<IDXGIResource> dxgiResource;
  HRESULT hr = sharedTexture.As(&dxgiResource);
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to get IDXGIResource, hr=0x%08X", hr);
    return false;
  }
  hr = dxgiResource->GetSharedHandle(&kmtHandle);
  if (FAILED(hr) || !kmtHandle) {
    LOGE("D3D11VideoStream: Failed to get KMT shared handle, hr=0x%08X", hr);
    return false;
  }

  uint64_t textureSize = 0;

  glGenTextures(1, &glTextureId);
  glCreateMemoryObjectsEXT(1, &glMemoryObject);

  if (glTextureId == 0 || glMemoryObject == 0) {
    LOGE("D3D11VideoStream: Failed to create GL texture=%u or memory object=%u.",
         glTextureId, glMemoryObject);
    releaseGLInteropMemoryObject();
    return false;
  }

  int dedicated = GL_TRUE;
  glMemoryObjectParameterivEXT(glMemoryObject, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);

  glImportMemoryWin32HandleEXT(glMemoryObject, textureSize,
                                GL_HANDLE_TYPE_D3D11_IMAGE_KMT_EXT, kmtHandle);

  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    LOGE("D3D11VideoStream: glImportMemoryWin32HandleEXT(KMT) failed, GL error=0x%04X", err);
    releaseGLInteropMemoryObject();
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, glTextureId);
  glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, width(), height(), glMemoryObject, 0);

  err = glGetError();
  if (err != GL_NO_ERROR) {
    LOGE("D3D11VideoStream: glTexStorageMem2DEXT failed, GL error=0x%04X", err);
    glBindTexture(GL_TEXTURE_2D, 0);
    releaseGLInteropMemoryObject();
    return false;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
  glBindTexture(GL_TEXTURE_2D, 0);

  LOGI("D3D11VideoStream: GL_EXT_memory_object (KMT) interop initialized, %dx%d",
       width(), height());
  return true;
}

void D3D11VideoStream::releaseGLInteropMemoryObject() {
  if (glTextureId != 0) {
    glDeleteTextures(1, &glTextureId);
    glTextureId = 0;
  }

  if (glDeleteMemoryObjectsEXT && glMemoryObject != 0) {
    glDeleteMemoryObjectsEXT(1, &glMemoryObject);
    glMemoryObject = 0;
  }

  interopInitialized = false;
}

void D3D11VideoStream::releaseGLInterop() {
  if (useMemoryObject) {
    releaseGLInteropMemoryObject();
    return;
  }

  if (textureLocked && glInteropTexture && wglDXUnlockObjectsNV && glInteropDevice) {
    wglDXUnlockObjectsNV(glInteropDevice, 1, reinterpret_cast<HANDLE*>(&glInteropTexture));
    textureLocked = false;
  }
  if (glInteropTexture && wglDXUnregisterObjectNV && glInteropDevice) {
    wglDXUnregisterObjectNV(glInteropDevice, glInteropTexture);
    glInteropTexture = nullptr;
  }

  if (glTextureId) {
    glDeleteTextures(1, &glTextureId);
    glTextureId = 0;
  }

  if (glInteropDevice) {
    ReleaseSharedInteropDevice(glInteropDevice, d3d11Device.Get());
    glInteropDevice = nullptr;
  }
}

std::shared_ptr<TextureView> D3D11VideoStream::onMakeTexture(Context* context, bool) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!ensureInitialized()) {
    return nullptr;
  }

  if (!initGLInterop()) {
    return nullptr;
  }

  // For WGL_NV_DX_interop path: lock the texture for OpenGL access
  if (!useMemoryObject) {
    if (glInteropTexture && wglDXLockObjectsNV && glInteropDevice) {
      if (!wglDXLockObjectsNV(glInteropDevice, 1,
                              reinterpret_cast<HANDLE*>(&glInteropTexture))) {
        LOGE("D3D11VideoStream: Failed to lock texture for GL access.");
        return nullptr;
      }
      textureLocked = true;
    }
  }

  TextureDescriptor descriptor = {width(),
                                  height(),
                                  PixelFormat::BGRA_8888,
                                  false,
                                  1,
                                  TextureUsage::TEXTURE_BINDING};

  auto gpu = static_cast<GLGPU*>(context->gpu());
  auto texture = gpu->makeResource<GLTexture>(descriptor, static_cast<unsigned>(GL_TEXTURE_2D),
                                              glTextureId);

  return Resource::AddToCache(context, new DefaultTextureView(std::move(texture)));
}

bool D3D11VideoStream::onUpdateTexture(std::shared_ptr<TextureView> textureView) {
  if (!hasPendingUpdate) {
    return true;
  }
  hasPendingUpdate = false;

  if (!useMemoryObject) {
    // WGL_NV_DX_interop path: unlock then re-lock the single texture to sync D3D11 writes
    if (textureLocked && glInteropTexture && wglDXUnlockObjectsNV && glInteropDevice) {
      wglDXUnlockObjectsNV(glInteropDevice, 1, reinterpret_cast<HANDLE*>(&glInteropTexture));
      textureLocked = false;
    }

    if (glInteropTexture && wglDXLockObjectsNV && glInteropDevice) {
      if (!wglDXLockObjectsNV(glInteropDevice, 1,
                              reinterpret_cast<HANDLE*>(&glInteropTexture))) {
        LOGE("D3D11VideoStream: Failed to lock texture for GL access in onUpdateTexture.");
        return false;
      }
      textureLocked = true;
    }
  }

  return true;
}

}  // namespace tgfx

#endif  // _WIN32
