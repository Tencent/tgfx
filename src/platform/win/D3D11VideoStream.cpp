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

#ifdef _WIN32

#include "D3D11VideoStream.h"
#include <windows.h>
#include <cstring>
#include <GL/gl.h>
#include "core/utils/Log.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLTexture.h"
#include "gpu/resources/DefaultTextureView.h"

// WGL_NV_DX_interop definitions
#define WGL_ACCESS_READ_ONLY_NV 0x0000
#define WGL_ACCESS_READ_WRITE_NV 0x0001
#define WGL_ACCESS_WRITE_DISCARD_NV 0x0002

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

std::shared_ptr<D3D11VideoStream> D3D11VideoStream::Make(int width, int height, ID3D11Device* device,
                                                          ID3D11DeviceContext* context) {
  if (width <= 0 || height <= 0 || !device || !context) {
    return nullptr;
  }

  if (!IsNVDXInteropAvailable()) {
    return nullptr;
  }

  auto stream =
      std::shared_ptr<D3D11VideoStream>(new D3D11VideoStream(width, height, device, context));
  if (!stream->initialize()) {
    return nullptr;
  }
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
  outputView.Reset();
  videoProcessor.Reset();
  videoProcessorEnum.Reset();
  videoContext.Reset();
  videoDevice.Reset();
  sharedTexture.Reset();
  d3d11Context.Reset();
  d3d11Device.Reset();
}

bool D3D11VideoStream::initialize() {
  if (!initSharedTexture()) {
    return false;
  }
  if (!initVideoProcessor()) {
    return false;
  }
  return true;
}

bool D3D11VideoStream::initSharedTexture() {
  if (!d3d11Device) {
    LOGE("D3D11VideoStream: D3D11 device is null.");
    return false;
  }

  // Create shared texture for D3D11-OpenGL interop (BGRA format)
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(width());
  desc.Height = static_cast<UINT>(height());
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Compatible with OpenGL BGRA
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;  // Enable sharing with OpenGL

  HRESULT hr = d3d11Device->CreateTexture2D(&desc, nullptr, sharedTexture.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create shared texture, hr=0x%08X", hr);
    return false;
  }

  return true;
}

bool D3D11VideoStream::initVideoProcessor() {
  // Get video device interface
  HRESULT hr = d3d11Device.As(&videoDevice);
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to get ID3D11VideoDevice, hr=0x%08X", hr);
    return false;
  }

  // Get video context interface
  hr = d3d11Context.As(&videoContext);
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to get ID3D11VideoContext, hr=0x%08X", hr);
    return false;
  }

  // Create video processor enumerator
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
  contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  contentDesc.InputWidth = static_cast<UINT>(width());
  contentDesc.InputHeight = static_cast<UINT>(height());
  contentDesc.OutputWidth = static_cast<UINT>(width());
  contentDesc.OutputHeight = static_cast<UINT>(height());
  contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  hr = videoDevice->CreateVideoProcessorEnumerator(&contentDesc, videoProcessorEnum.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create video processor enumerator, hr=0x%08X", hr);
    return false;
  }

  // Create video processor
  hr = videoDevice->CreateVideoProcessor(videoProcessorEnum.Get(), 0, videoProcessor.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create video processor, hr=0x%08X", hr);
    return false;
  }

  // Create output view for the shared texture
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {};
  outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  outputViewDesc.Texture2D.MipSlice = 0;

  hr = videoDevice->CreateVideoProcessorOutputView(sharedTexture.Get(), videoProcessorEnum.Get(),
                                                    &outputViewDesc, outputView.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create video processor output view, hr=0x%08X", hr);
    return false;
  }

  return true;
}

bool D3D11VideoStream::updateTexture(ID3D11Texture2D* decodedTexture, int subresourceIndex) {
  std::lock_guard<std::mutex> lock(locker);

  if (!decodedTexture || !sharedTexture || !videoProcessor || !outputView) {
    return false;
  }

  // Use a query to wait for GPU to complete any pending operations on the decoded texture
  D3D11_QUERY_DESC queryDesc = {};
  queryDesc.Query = D3D11_QUERY_EVENT;
  ComPtr<ID3D11Query> query;
  HRESULT hr = d3d11Device->CreateQuery(&queryDesc, query.GetAddressOf());
  if (SUCCEEDED(hr)) {
    d3d11Context->End(query.Get());
    d3d11Context->Flush();
    BOOL queryData = FALSE;
    while (d3d11Context->GetData(query.Get(), &queryData, sizeof(queryData), 0) == S_FALSE) {
      // Spin wait for GPU to complete
    }
  } else {
    d3d11Context->Flush();
  }

  // Create input view for the decoded texture (NV12 format)
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
  inputViewDesc.FourCC = 0;
  inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  inputViewDesc.Texture2D.MipSlice = 0;
  inputViewDesc.Texture2D.ArraySlice = static_cast<UINT>(subresourceIndex);

  ComPtr<ID3D11VideoProcessorInputView> inputView;
  hr = videoDevice->CreateVideoProcessorInputView(decodedTexture, videoProcessorEnum.Get(),
                                                           &inputViewDesc, inputView.GetAddressOf());
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: Failed to create input view, hr=0x%08X", hr);
    return false;
  }

  // Set input color space: assume BT.709 with limited range (typical for video)
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColorSpace = {};
  inputColorSpace.Usage = 0;           // Playback
  inputColorSpace.RGB_Range = 1;       // Limited range (16-235)
  inputColorSpace.YCbCr_Matrix = 1;    // BT.709
  inputColorSpace.YCbCr_xvYCC = 0;     // Conventional YCbCr
  inputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  videoContext->VideoProcessorSetStreamColorSpace(videoProcessor.Get(), 0, &inputColorSpace);

  // Set output color space: full range RGB for OpenGL
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColorSpace = {};
  outputColorSpace.Usage = 0;          // Playback
  outputColorSpace.RGB_Range = 0;      // Full range (0-255)
  outputColorSpace.YCbCr_Matrix = 1;   // BT.709
  outputColorSpace.YCbCr_xvYCC = 0;
  outputColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
  videoContext->VideoProcessorSetOutputColorSpace(videoProcessor.Get(), &outputColorSpace);

  // Set up the stream data
  D3D11_VIDEO_PROCESSOR_STREAM stream = {};
  stream.Enable = TRUE;
  stream.OutputIndex = 0;
  stream.InputFrameOrField = 0;
  stream.PastFrames = 0;
  stream.FutureFrames = 0;
  stream.ppPastSurfaces = nullptr;
  stream.pInputSurface = inputView.Get();
  stream.ppFutureSurfaces = nullptr;


  // Process: convert NV12 to BGRA
  hr = videoContext->VideoProcessorBlt(videoProcessor.Get(), outputView.Get(), 0, 1, &stream);
  if (FAILED(hr)) {
    LOGE("D3D11VideoStream: VideoProcessorBlt failed, hr=0x%08X", hr);
    return false;
  }

  hasPendingUpdate = true;
  return true;
}

bool D3D11VideoStream::initGLInterop() {
  if (interopInitialized) {
    return glInteropTexture != nullptr;
  }
  interopInitialized = true;

  if (!IsNVDXInteropAvailable() || !d3d11Device || !sharedTexture) {
    return false;
  }

  // Open D3D11 device for OpenGL interop
  glInteropDevice = wglDXOpenDeviceNV(d3d11Device.Get());
  if (!glInteropDevice) {
    LOGE("D3D11VideoStream: Failed to open D3D11 device for GL interop.");
    return false;
  }

  // Create OpenGL texture
  glGenTextures(1, &glTextureId);
  if (glTextureId == 0) {
    LOGE("D3D11VideoStream: Failed to create OpenGL texture.");
    wglDXCloseDeviceNV(glInteropDevice);
    glInteropDevice = nullptr;
    return false;
  }

  // Register D3D11 texture with OpenGL
  glInteropTexture = wglDXRegisterObjectNV(glInteropDevice, sharedTexture.Get(), glTextureId,
                                           GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);

  if (!glInteropTexture) {
    LOGE("D3D11VideoStream: Failed to register D3D11 texture with OpenGL.");
    glDeleteTextures(1, &glTextureId);
    glTextureId = 0;
    wglDXCloseDeviceNV(glInteropDevice);
    glInteropDevice = nullptr;
    return false;
  }

  return true;
}

void D3D11VideoStream::releaseGLInterop() {
  // Unlock texture before unregistering
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

  if (glInteropDevice && wglDXCloseDeviceNV) {
    wglDXCloseDeviceNV(glInteropDevice);
    glInteropDevice = nullptr;
  }
}

std::shared_ptr<TextureView> D3D11VideoStream::onMakeTexture(Context* context, bool) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!initGLInterop()) {
    return nullptr;
  }

  // Lock the texture for OpenGL access
  if (glInteropTexture && wglDXLockObjectsNV && glInteropDevice) {
    if (!wglDXLockObjectsNV(glInteropDevice, 1, reinterpret_cast<HANDLE*>(&glInteropTexture))) {
      LOGE("D3D11VideoStream: Failed to lock texture for GL access.");
      return nullptr;
    }
    textureLocked = true;
  }

  // Create texture descriptor
  // D3D11 shared texture is BGRA format (DXGI_FORMAT_B8G8R8A8_UNORM)
  TextureDescriptor descriptor = {width(),
                                  height(),
                                  PixelFormat::BGRA_8888,
                                  false,
                                  1,
                                  TextureUsage::TEXTURE_BINDING};

  // Create GLTexture from the existing GL texture ID
  auto gpu = static_cast<GLGPU*>(context->gpu());
  auto texture =
      gpu->makeResource<GLTexture>(descriptor, static_cast<unsigned>(GL_TEXTURE_2D), glTextureId);

  return Resource::AddToCache(context, new DefaultTextureView(std::move(texture)));
}

bool D3D11VideoStream::onUpdateTexture(std::shared_ptr<TextureView>) {
  // Only unlock/lock if there's actually new data from D3D11
  if (!hasPendingUpdate) {
    // No new frame, keep the texture locked for OpenGL to continue using
    return true;
  }
  hasPendingUpdate = false;

  // Unlock the texture so D3D11 can write to it
  if (textureLocked && glInteropTexture && wglDXUnlockObjectsNV && glInteropDevice) {
    wglDXUnlockObjectsNV(glInteropDevice, 1, reinterpret_cast<HANDLE*>(&glInteropTexture));
    textureLocked = false;
  }

  // Lock the texture for OpenGL access after D3D11 has finished writing
  if (glInteropTexture && wglDXLockObjectsNV && glInteropDevice) {
    if (!wglDXLockObjectsNV(glInteropDevice, 1, reinterpret_cast<HANDLE*>(&glInteropTexture))) {
      LOGE("D3D11VideoStream: Failed to lock texture for GL access in onUpdateTexture.");
      return false;
    }
    textureLocked = true;
  }

  return true;
}

}  // namespace tgfx

#endif  // _WIN32
