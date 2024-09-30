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

#include "TGFXWindow.h"
#include <shellscalingapi.h>
#include <filesystem>

namespace hello2d {
static constexpr LPCWSTR ClassName = L"TGFXWindow";

TGFXWindow::TGFXWindow() {
  createAppHost();
}

TGFXWindow::~TGFXWindow() {
  destroy();
}

bool TGFXWindow::open() {
  destroy();
  WNDCLASS windowClass = RegisterWindowClass();
  auto pixelRatio = getPixelRatio();
  int initWidth = static_cast<int>(pixelRatio * 800);
  int initHeight = static_cast<int>(pixelRatio * 600);
  windowHandle =
      CreateWindowEx(WS_EX_APPWINDOW, windowClass.lpszClassName, L"Hello2D", WS_OVERLAPPEDWINDOW, 0,
                     0, initWidth, initHeight, nullptr, nullptr, windowClass.hInstance, this);

  if (windowHandle == nullptr) {
    return false;
  }
  SetWindowLongPtr(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  centerAndShow();
  return true;
}

WNDCLASS TGFXWindow::RegisterWindowClass() {
  auto hInstance = GetModuleHandle(nullptr);
  WNDCLASS windowClass{};
  windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  windowClass.lpszClassName = ClassName;
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.cbClsExtra = 0;
  windowClass.cbWndExtra = 0;
  windowClass.hInstance = hInstance;
  windowClass.hIcon = LoadIcon(hInstance, L"IDI_ICON1");
  windowClass.hbrBackground = nullptr;
  windowClass.lpszMenuName = nullptr;
  windowClass.lpfnWndProc = WndProc;
  RegisterClass(&windowClass);
  return windowClass;
}

LRESULT CALLBACK TGFXWindow::WndProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam) noexcept {
  auto tgfxWindow = reinterpret_cast<TGFXWindow*>(GetWindowLongPtr(window, GWLP_USERDATA));
  if (tgfxWindow != nullptr) {
    return tgfxWindow->handleMessage(window, message, wparam, lparam);
  }
  return DefWindowProc(window, message, wparam, lparam);
}

LRESULT TGFXWindow::handleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
  LRESULT result = 0;
  switch (message) {
    case WM_DESTROY:
      destroy();
      PostQuitMessage(0);
      break;
    case WM_PAINT:
      draw();
      break;
    case WM_LBUTTONDOWN:
      lastDrawIndex++;
      ::InvalidateRect(windowHandle, nullptr, TRUE);
      break;
    default:
      result = DefWindowProc(windowHandle, message, wparam, lparam);
      break;
  }
  return result;
}

void TGFXWindow::destroy() {
  if (windowHandle) {
    DestroyWindow(windowHandle);
    windowHandle = nullptr;
    UnregisterClass(ClassName, nullptr);
  }
}

void TGFXWindow::centerAndShow() {
  if ((GetWindowStyle(windowHandle) & WS_CHILD) != 0) {
    return;
  }
  RECT rcDlg = {0};
  ::GetWindowRect(windowHandle, &rcDlg);
  RECT rcArea = {0};
  RECT rcCenter = {0};
  HWND hWnd = windowHandle;
  HWND hWndCenter = ::GetWindowOwner(windowHandle);
  if (hWndCenter != nullptr) {
    hWnd = hWndCenter;
  }

  MONITORINFO oMonitor = {};
  oMonitor.cbSize = sizeof(oMonitor);
  ::GetMonitorInfo(::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &oMonitor);
  rcArea = oMonitor.rcWork;

  if (hWndCenter == nullptr) {
    rcCenter = rcArea;
  } else {
    ::GetWindowRect(hWndCenter, &rcCenter);
  }

  int DlgWidth = rcDlg.right - rcDlg.left;
  int DlgHeight = rcDlg.bottom - rcDlg.top;

  // Find dialog's upper left based on rcCenter
  int xLeft = (rcCenter.left + rcCenter.right) / 2 - DlgWidth / 2;
  int yTop = (rcCenter.top + rcCenter.bottom) / 2 - DlgHeight / 2;

  // The dialog is outside the screen, move it inside
  if (xLeft < rcArea.left) {
    if (xLeft < 0) {
      xLeft = GetSystemMetrics(SM_CXSCREEN) / 2 - DlgWidth / 2;
    } else {
      xLeft = rcArea.left;
    }
  } else if (xLeft + DlgWidth > rcArea.right) {
    xLeft = rcArea.right - DlgWidth;
  }

  if (yTop < rcArea.top) {
    if (yTop < 0) {
      yTop = GetSystemMetrics(SM_CYSCREEN) / 2 - DlgHeight / 2;
    } else {
      yTop = rcArea.top;
    }

  } else if (yTop + DlgHeight > rcArea.bottom) {
    yTop = rcArea.bottom - DlgHeight;
  }
  ::SetWindowPos(windowHandle, nullptr, xLeft, yTop, -1, -1,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

float TGFXWindow::getPixelRatio() {
  UINT dpi = 96;
  if (windowHandle != nullptr) {
    auto monitor = ::MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96;
    UINT dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpi = dpiX;
  } else {
    dpi = GetDpiForSystem();
  }
  return static_cast<float>(dpi) / 96.0f;
}

void TGFXWindow::createAppHost() {
  appHost = std::make_unique<drawers::AppHost>();
  std::filesystem::path filePath = __FILE__;
  auto rootPath = filePath.parent_path().parent_path().parent_path().string();
  auto imagePath = rootPath + R"(\resources\assets\bridge.jpg)";
  auto image = tgfx::Image::MakeFromFile(imagePath);
  appHost->addImage("bridge", image);
  auto typeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
  appHost->addTypeface("default", typeface);
  typeface = tgfx::Typeface::MakeFromName("Segoe UI Emoji", "");
  appHost->addTypeface("emoji", typeface);
}

void TGFXWindow::draw() {
  if (!tgfxWindow) {
    tgfxWindow = tgfx::EGLWindow::MakeFrom(windowHandle);
  }
  if (tgfxWindow == nullptr) {
    return;
  }
  RECT rect;
  GetClientRect(windowHandle, &rect);
  auto width = static_cast<int>(rect.right - rect.left);
  auto height = static_cast<int>(rect.bottom - rect.top);
  auto pixelRatio = getPixelRatio();
  auto sizeChanged = appHost->updateScreen(width, height, pixelRatio);
  if (sizeChanged) {
    tgfxWindow->invalidSize();
  }
  auto device = tgfxWindow->getDevice();
  if (device == nullptr) {
    return;
  }
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  auto surface = tgfxWindow->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->save();
  auto numDrawers = drawers::Drawer::Count() - 1;
  auto index = (lastDrawIndex % numDrawers) + 1;
  auto drawer = drawers::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost.get());
  drawer = drawers::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost.get());
  canvas->restore();
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}
}  // namespace hello2d