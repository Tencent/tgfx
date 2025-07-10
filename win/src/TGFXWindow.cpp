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

#include "TGFXWindow.h"
#include <cmath>
#include <filesystem>
#if WINVER >= 0x0603  // Windows 8.1
#include <shellscalingapi.h>
#endif

namespace hello2d {
static constexpr LPCWSTR ClassName = L"TGFXWindow";
static constexpr float MAX_ZOOM = 1000.0f;
static constexpr float MIN_ZOOM = 0.001f;
static constexpr float WHEEL_RATIO = 400.0f;

TGFXWindow::TGFXWindow() {
  createAppHost();
}

TGFXWindow::~TGFXWindow() {
  destroy();
}

bool TGFXWindow::open() {
  destroy();
  WNDCLASS windowClass = RegisterWindowClass();
  float pixelRatio = getPixelRatio();
  int initWidth = static_cast<int>(pixelRatio * 800);
  int initHeight = static_cast<int>(pixelRatio * 600);
  windowHandle =
      CreateWindowEx(WS_EX_APPWINDOW, windowClass.lpszClassName, L"Hello2D", WS_OVERLAPPEDWINDOW, 0,
                     0, initWidth, initHeight, nullptr, nullptr, windowClass.hInstance, this);

  if (windowHandle == nullptr) {
    return false;
  }
  RegisterTouchWindow(windowHandle, 0);
  SetWindowLongPtr(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  centerAndShow();
  ShowWindow(windowHandle, SW_SHOW);
  UpdateWindow(windowHandle);
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
  switch (message) {
    case WM_DESTROY:
      destroy();
      PostQuitMessage(0);
      break;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(windowHandle, &ps);
      draw();
      EndPaint(windowHandle, &ps);
      break;
    }
    case WM_LBUTTONDOWN: {
      currentDrawerIndex++;
      zoomScale = 1.0f;
      contentOffset = {0.0f, 0.0f};
      ::InvalidateRect(windowHandle, nullptr, TRUE);
      break;
    }
    case WM_MOUSEWHEEL: {
      POINT mousePoint = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &mousePoint);
      bool isCtrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      float pixelRatio = getPixelRatio();
      float mouseX = mousePoint.x * pixelRatio;
      float mouseY = mousePoint.y * pixelRatio;
      if (isCtrlPressed) {
        float zoomStep = std::exp(GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_RATIO);
        float newZoom = std::clamp(zoomScale * zoomStep, MIN_ZOOM, MAX_ZOOM);
        float contentX = (mouseX - contentOffset.x) / zoomScale;
        float contentY = (mouseY - contentOffset.y) / zoomScale;
        contentOffset.x = mouseX - contentX * newZoom;
        contentOffset.y = mouseY - contentY * newZoom;
        zoomScale = newZoom;
      } else {
        float wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam));
        if (isShiftPressed) {
          contentOffset.x += wheelDelta;
        } else {
          contentOffset.y -= wheelDelta;
        }
      }
      ::InvalidateRect(windowHandle, nullptr, TRUE);
      break;
    }
    case WM_GESTURE: {
      GESTUREINFO gestureInfo{};
      gestureInfo.cbSize = sizeof(GESTUREINFO);
      if (GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lparam), &gestureInfo)) {
        static double lastZoomArgument = 0.0;
        if (gestureInfo.dwID == GID_ZOOM) {
          double currentArgument = gestureInfo.ullArguments;
          if (lastZoomArgument != 0.0) {
            double zoomFactor = currentArgument / lastZoomArgument;
            POINT gesturePoint = {gestureInfo.ptsLocation.x, gestureInfo.ptsLocation.y};
            ScreenToClient(hwnd, &gesturePoint);
            float pixelRatio = getPixelRatio();
            float mouseX = gesturePoint.x * pixelRatio;
            float mouseY = gesturePoint.y * pixelRatio;
            float newZoom =
                std::clamp(zoomScale * static_cast<float>(zoomFactor), MIN_ZOOM, MAX_ZOOM);
            float contentX = (mouseX - contentOffset.x) / zoomScale;
            float contentY = (mouseY - contentOffset.y) / zoomScale;
            contentOffset.x = mouseX - contentX * newZoom;
            contentOffset.y = mouseY - contentY * newZoom;
            zoomScale = newZoom;
            ::InvalidateRect(windowHandle, nullptr, TRUE);
          }
          lastZoomArgument = currentArgument;
        }
        if (gestureInfo.dwFlags & GF_END) {
          lastZoomArgument = 0.0;
        }
        CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lparam));
      }
      break;
    }
    default:
      return DefWindowProc(windowHandle, message, wparam, lparam);
  }
  return 0;
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

  RECT windowRect;
  ::GetWindowRect(windowHandle, &windowRect);
  int windowWidth = windowRect.right - windowRect.left;
  int windowHeight = windowRect.bottom - windowRect.top;

  HWND centerWindow = ::GetWindowOwner(windowHandle);
  RECT centerRect;

  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  ::GetMonitorInfo(
      ::MonitorFromWindow(centerWindow ? centerWindow : windowHandle, MONITOR_DEFAULTTONEAREST),
      &monitorInfo);
  RECT workArea = monitorInfo.rcWork;

  if (centerWindow == nullptr) {
    centerRect = workArea;
  } else {
    ::GetWindowRect(centerWindow, &centerRect);
  }
  int xPos = (centerRect.left + centerRect.right - windowWidth) / 2;
  int yPos = (centerRect.top + centerRect.bottom - windowHeight) / 2;
  xPos = std::clamp(xPos, workArea.left, workArea.right - windowWidth);
  yPos = std::clamp(yPos, workArea.top, workArea.bottom - windowHeight);

  ::SetWindowPos(windowHandle, nullptr, xPos, yPos, -1, -1,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

float TGFXWindow::getPixelRatio() {
#if WINVER >= 0x0603  // Windows 8.1
  HMONITOR monitor = windowHandle ? ::MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST)
                                  : ::MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
  UINT dpiX = 96;
  UINT dpiY = 96;
  GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  return static_cast<float>(dpiX) / 96.0f;
#else
  return 1.0f;
#endif
}

void TGFXWindow::createAppHost() {
  appHost = std::make_unique<drawers::AppHost>();
  std::filesystem::path currentFile = __FILE__;
  std::string rootPath = currentFile.parent_path().parent_path().parent_path().string();
  std::string imagePath = rootPath + R"(\resources\assets\bridge.jpg)";
  auto image = tgfx::Image::MakeFromFile(imagePath);
  appHost->addImage("bridge", image);
  auto defaultTypeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
  appHost->addTypeface("default", defaultTypeface);
  std::string emojiPath = rootPath + R"(\resources\font\NotoColorEmoji.ttf)";
  auto emojiTypeface = tgfx::Typeface::MakeFromPath(emojiPath);
  appHost->addTypeface("emoji", emojiTypeface);
}

void TGFXWindow::draw() {
  if (!tgfxWindow) {
    tgfxWindow = tgfx::WGLWindow::MakeFrom(windowHandle);
  }
  if (tgfxWindow == nullptr) {
    return;
  }

  RECT clientRect;
  GetClientRect(windowHandle, &clientRect);
  int width = clientRect.right - clientRect.left;
  int height = clientRect.bottom - clientRect.top;
  float pixelRatio = getPixelRatio();
  bool sizeChanged = appHost->updateScreen(width, height, pixelRatio);
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
  appHost->updateZoomAndOffset(zoomScale, contentOffset);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->save();
  int numDrawers = drawers::Drawer::Count() - 1;
  int drawerIndex = (currentDrawerIndex % numDrawers) + 1;
  auto backgroundDrawer = drawers::Drawer::GetByName("GridBackground");
  backgroundDrawer->draw(canvas, appHost.get());
  auto currentDrawer = drawers::Drawer::GetByIndex(drawerIndex);
  currentDrawer->draw(canvas, appHost.get());
  canvas->restore();
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}
}  // namespace hello2d