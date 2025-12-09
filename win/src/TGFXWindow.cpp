// win/src/TGFXWindow.cpp
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
#include <algorithm>
#include <cmath>
#include <filesystem>
#if WINVER >= 0x0603  // Windows 8.1
#include <shellscalingapi.h>
#endif
#include "hello2d/AppHost.h"
#include "hello2d/LayerBuilder.h"

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
  auto pixelRatio = getPixelRatio();
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
  ::InvalidateRect(windowHandle, nullptr, FALSE);
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
    case WM_ACTIVATE:
      isDrawing = (LOWORD(wparam) != WA_INACTIVE);
      break;
    case WM_DESTROY:
      destroy();
      PostQuitMessage(0);
      break;
    case WM_SIZE:
      if (tgfxWindow) {
        tgfxWindow->invalidSize();
        // Clear lastRecording when size changes, as it was created for the old surface size
        lastRecording = nullptr;
        // Mark size as invalidated to force render next frame
        sizeInvalidated = true;
      }
      ::InvalidateRect(windowHandle, nullptr, FALSE);
      break;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      if (isDrawing) {
        bool hasContentChanged = draw();
        if (hasContentChanged) {
          ::InvalidateRect(windowHandle, nullptr, FALSE);
        }
      }
      EndPaint(hwnd, &ps);
      break;
    }
    case WM_LBUTTONUP: {
      int count = hello2d::LayerBuilder::Count();
      if (count > 0) {
        currentDrawerIndex = (currentDrawerIndex + 1) % count;
        zoomScale = 1.0f;
        contentOffset = {0.0f, 0.0f};
        ::InvalidateRect(windowHandle, nullptr, FALSE);
      }
      break;
    }
    case WM_MOUSEWHEEL: {
      POINT mousePoint = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &mousePoint);
      bool isCtrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

      if (isCtrlPressed) {
        float zoomStep = std::exp(GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_RATIO);
        float newZoom = std::clamp(zoomScale * zoomStep, MIN_ZOOM, MAX_ZOOM);
        float oldZoom = zoomScale;
        contentOffset.x = mousePoint.x - ((mousePoint.x - contentOffset.x) / oldZoom) * newZoom;
        contentOffset.y = mousePoint.y - ((mousePoint.y - contentOffset.y) / oldZoom) * newZoom;
        zoomScale = newZoom;
      } else {
        float wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam));
        if (isShiftPressed) {
          contentOffset.x += wheelDelta;
        } else {
          contentOffset.y -= wheelDelta;
        }
      }
      ::InvalidateRect(windowHandle, nullptr, FALSE);
      break;
    }
    case WM_GESTURE: {
      GESTUREINFO gestureInfo{};
      gestureInfo.cbSize = sizeof(GESTUREINFO);
      if (GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lparam), &gestureInfo)) {
        if (gestureInfo.dwID == GID_ZOOM) {
          double currentArgument = static_cast<double>(gestureInfo.ullArguments);
          if (lastZoomArgument != 0.0) {
            double zoomFactor = currentArgument / lastZoomArgument;
            POINT mousePoint = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &mousePoint);
            float newZoom =
                std::clamp(zoomScale * static_cast<float>(zoomFactor), MIN_ZOOM, MAX_ZOOM);
            float oldZoom = zoomScale;
            contentOffset.x = mousePoint.x - ((mousePoint.x - contentOffset.x) / oldZoom) * newZoom;
            contentOffset.y = mousePoint.y - ((mousePoint.y - contentOffset.y) / oldZoom) * newZoom;
            zoomScale = newZoom;
          }
          lastZoomArgument = currentArgument;
        }
        if (gestureInfo.dwFlags & GF_END) {
          lastZoomArgument = 0.0;
        }
        CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lparam));
        ::InvalidateRect(windowHandle, nullptr, FALSE);
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

  int xLeft = (rcCenter.left + rcCenter.right) / 2 - DlgWidth / 2;
  int yTop = (rcCenter.top + rcCenter.bottom) / 2 - DlgHeight / 2;

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
#if WINVER >= 0x0603
  HMONITOR monitor = nullptr;
  if (windowHandle != nullptr) {
    monitor = ::MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
  } else {
    monitor = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  }
  UINT dpiX = 96;
  UINT dpiY = 96;
  GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  return static_cast<float>(dpiX) / 96.0f;
#else
  return 1.0f;
#endif
}

void TGFXWindow::createAppHost() {
  appHost = std::make_unique<hello2d::AppHost>();

  displayList.setRenderMode(tgfx::RenderMode::Tiled);
  displayList.setAllowZoomBlur(true);
  displayList.setMaxTileCount(512);

  std::filesystem::path filePath = __FILE__;
  auto rootPath = filePath.parent_path().parent_path().parent_path().string();
  auto imagePath = rootPath + R"(\resources\assets\bridge.jpg)";
  auto image = tgfx::Image::MakeFromFile(imagePath);
  appHost->addImage("bridge", image);
  imagePath = rootPath + R"(\resources\assets\tgfx.png)";
  appHost->addImage("TGFX", tgfx::Image::MakeFromFile(imagePath));
  auto typeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
  appHost->addTypeface("default", typeface);
  auto emojiPath = rootPath + R"(\resources\font\NotoColorEmoji.ttf)";
  typeface = tgfx::Typeface::MakeFromPath(emojiPath);
  appHost->addTypeface("emoji", typeface);
}

bool TGFXWindow::draw() {
  if (!tgfxWindow) {
    tgfxWindow = tgfx::WGLWindow::MakeFrom(windowHandle);
  }
  if (tgfxWindow == nullptr) {
    return false;
  }
  RECT rect;
  GetClientRect(windowHandle, &rect);
  auto width = static_cast<int>(rect.right - rect.left);
  auto height = static_cast<int>(rect.bottom - rect.top);
  if (width <= 0 || height <= 0) {
    return false;
  }
  auto pixelRatio = getPixelRatio();

  // ========== All DisplayList updates BEFORE locking device ==========

  int count = hello2d::LayerBuilder::Count();
  int index = (count > 0) ? (currentDrawerIndex % count) : 0;
  if (index != lastDrawIndex || !contentLayer) {
    auto builder = hello2d::LayerBuilder::GetByIndex(index);
    if (builder) {
      contentLayer = builder->buildLayerTree(appHost.get());
      if (contentLayer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(contentLayer);
      }
    }
    lastDrawIndex = index;
  }

  // Calculate base scale and offset using cached surface size
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    bool zoomChanged = (zoomScale != lastAppliedZoomScale);
    bool offsetChanged =
        (contentOffset.x != lastAppliedContentOffset.x ||
         contentOffset.y != lastAppliedContentOffset.y);
    if (zoomChanged || offsetChanged) {
      static constexpr float DESIGN_SIZE = 720.0f;
      auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
      auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
      auto baseScale = std::min(scaleX, scaleY);
      auto scaledSize = DESIGN_SIZE * baseScale;
      auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
      auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;

      auto finalZoomScale = zoomScale * baseScale;
      auto finalOffsetX = baseOffsetX + contentOffset.x;
      auto finalOffsetY = baseOffsetY + contentOffset.y;

      displayList.setZoomScale(finalZoomScale);
      displayList.setContentOffset(finalOffsetX, finalOffsetY);
      lastAppliedZoomScale = zoomScale;
      lastAppliedContentOffset = contentOffset;
    }
  }

  // Check if content has changed AFTER setting all properties, BEFORE locking device
  bool needsRender = displayList.hasContentChanged() || sizeInvalidated;

  // If no content change AND no pending lastRecording -> skip everything, don't lock device
  if (!needsRender && lastRecording == nullptr) {
    return false;
  }

  // ========== Now lock device for rendering/submitting ==========

  auto device = tgfxWindow->getDevice();
  if (device == nullptr) {
    return false;
  }
  auto context = device->lockContext();
  if (context == nullptr) {
    return false;
  }
  auto surface = tgfxWindow->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return false;
  }

  // Update cached surface size for next frame's calculations
  int newSurfaceWidth = surface->width();
  int newSurfaceHeight = surface->height();
  bool sizeChanged = (newSurfaceWidth != lastSurfaceWidth || newSurfaceHeight != lastSurfaceHeight);
  lastSurfaceWidth = newSurfaceWidth;
  lastSurfaceHeight = newSurfaceHeight;

  // If surface size just changed, update zoomScale/contentOffset now
  if (sizeChanged && lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    static constexpr float DESIGN_SIZE = 720.0f;
    auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
    auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
    auto baseScale = std::min(scaleX, scaleY);
    auto scaledSize = DESIGN_SIZE * baseScale;
    auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
    auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;
    displayList.setZoomScale(zoomScale * baseScale);
    displayList.setContentOffset(baseOffsetX + contentOffset.x, baseOffsetY + contentOffset.y);
    lastAppliedZoomScale = zoomScale;
    lastAppliedContentOffset = contentOffset;
    needsRender = true;
  }

  // Clear sizeInvalidated flag
  sizeInvalidated = false;

  // Track if we submitted anything this frame
  bool didSubmit = false;

  // Case 1: No content change BUT have pending lastRecording -> only submit lastRecording
  if (!needsRender) {
    context->submit(std::move(lastRecording));
    tgfxWindow->present(context);
    didSubmit = true;
    device->unlock();
    return didSubmit;
  }

  // Case 2: Content changed -> render new content
  auto canvas = surface->getCanvas();
  canvas->clear();
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), pixelRatio);

  displayList.render(surface.get(), false);

  // Delayed one-frame present mode
  auto recording = context->flush();
  if (lastRecording) {
    // Normal delayed mode: submit last frame, save current for next
    context->submit(std::move(lastRecording));
    tgfxWindow->present(context);
    didSubmit = true;
    lastRecording = std::move(recording);
  } else if (recording) {
    // No lastRecording (first frame or after size change): submit current directly
    context->submit(std::move(recording));
    tgfxWindow->present(context);
    didSubmit = true;
  }

  device->unlock();

  // Return true if we submitted or have pending recording
  return didSubmit || lastRecording != nullptr;
}
}  // namespace hello2d
