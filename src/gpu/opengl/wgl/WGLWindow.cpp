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

#include "tgfx/gpu/opengl/wgl/WGLWindow.h"
#include <GL/GL.h>
#include <icm.h>
#include "core/utils/Log.h"

namespace tgfx {

static BOOL GetMonitorICCFromHWND(HWND hWnd, wchar_t* pProfilePath, DWORD* pSize) {
  HDC hdc = GetDC(hWnd);
  if (hdc == NULL) {
    return FALSE;
  }

  DWORD dwRequiredSize = 0;
  BOOL bResult = GetICMProfileW(hdc, &dwRequiredSize, NULL);
  if (!bResult && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    if (pProfilePath && *pSize >= dwRequiredSize) {
      bResult = GetICMProfileW(hdc, pSize, pProfilePath);
      return bResult;
    } else {
      *pSize = dwRequiredSize;
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
  }
  return bResult;
}

static BOOL LoadICCProfileData(const wchar_t* pProfilePath, BYTE** ppProfileData,
                               DWORD* pDataSize) {
  if (pProfilePath == NULL || ppProfileData == NULL || pDataSize == NULL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  HANDLE hFile = CreateFileW(pProfilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    return FALSE;
  }

  DWORD dwFileSize = GetFileSize(hFile, NULL);
  if (dwFileSize == INVALID_FILE_SIZE) {
    CloseHandle(hFile);
    return FALSE;
  }

  BYTE* pData = (BYTE*)malloc(dwFileSize);
  if (pData == NULL) {
    CloseHandle(hFile);
    SetLastError(ERROR_OUTOFMEMORY);
    return FALSE;
  }

  DWORD dwBytesRead = 0;
  BOOL bReadResult = ReadFile(hFile, pData, dwFileSize, &dwBytesRead, NULL);
  CloseHandle(hFile);

  if (!bReadResult || dwBytesRead != dwFileSize) {
    free(pData);
    return FALSE;
  }

  *ppProfileData = pData;
  *pDataSize = dwFileSize;

  return TRUE;
}

std::shared_ptr<WGLWindow> WGLWindow::MakeFrom(HWND nativeWindow, HGLRC sharedContext) {
  if (nativeWindow == nullptr) {
    return nullptr;
  }

  auto device = WGLDevice::MakeFrom(nativeWindow, sharedContext);
  if (device == nullptr) {
    return nullptr;
  }
  auto wglWindow = std::shared_ptr<WGLWindow>(new WGLWindow(device));
  wglWindow->nativeWindow = nativeWindow;
  return wglWindow;
}

WGLWindow::WGLWindow(std::shared_ptr<Device> device) : Window(std::move(device)) {
}

std::shared_ptr<Surface> WGLWindow::onCreateSurface(Context* context) {
  ISize size = {0, 0};
  if (nativeWindow) {
    RECT rect = {};
    GetClientRect(nativeWindow, &rect);
    size.width = static_cast<int>(rect.right - rect.left);
    size.height = static_cast<int>(rect.bottom - rect.top);
  }
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }

  GLFrameBufferInfo frameBuffer = {0, GL_RGBA8};
  BackendRenderTarget renderTarget = {frameBuffer, size.width, size.height};
  std::shared_ptr<ColorSpace> colorSpace = nullptr;
  wchar_t profilePath[MAX_PATH];
  DWORD pathSize = MAX_PATH;
  if (GetMonitorICCFromHWND(nativeWindow, profilePath, &pathSize)) {
    BYTE* pICCData = NULL;
    DWORD dwDataSize = 0;
    if (LoadICCProfileData(profilePath, &pICCData, &dwDataSize)) {
      colorSpace = ColorSpace::MakeFromICC(pICCData, static_cast<size_t>(dwDataSize));
      free(pICCData);
      pICCData = NULL;
      dwDataSize = 0;
    }
  }

  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft, 1, colorSpace);
}

void WGLWindow::onPresent(Context*, int64_t) {
  const auto wglDevice = std::static_pointer_cast<WGLDevice>(this->device);
  SwapBuffers(wglDevice->deviceContext);
}

}  // namespace tgfx
