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

#include "JTGFXView.h"
#include "hello2d/LayerBuilder.h"

namespace hello2d {
static jfieldID TGFXView_nativePtr;

void JTGFXView::updateSize() {
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  if (width > 0 && height > 0) {
    window->invalidSize();
    // Clear lastRecording when size changes, as it was created for the old surface size
    lastRecording = nullptr;
    // Mark size as invalidated to force render next frame
    sizeInvalidated = true;
  }
}

bool JTGFXView::draw(int drawIndex, float zoom, float offsetX, float offsetY) {
  // ========== All DisplayList updates BEFORE locking device ==========

  // Switch sample when drawIndex changes
  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = (drawIndex % numBuilders);
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
    bool zoomChanged = (zoom != lastZoom);
    bool offsetChanged = (offsetX != lastOffsetX || offsetY != lastOffsetY);
    if (zoomChanged || offsetChanged) {
      static constexpr float DESIGN_SIZE = 720.0f;
      auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
      auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
      auto baseScale = std::min(scaleX, scaleY);
      auto scaledSize = DESIGN_SIZE * baseScale;
      auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
      auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;

      displayList.setZoomScale(zoom * baseScale);
      displayList.setContentOffset(baseOffsetX + offsetX, baseOffsetY + offsetY);
      lastZoom = zoom;
      lastOffsetX = offsetX;
      lastOffsetY = offsetY;
    }
  }

  // Check if content has changed AFTER setting all properties, BEFORE locking device
  bool needsRender = displayList.hasContentChanged() || sizeInvalidated;

  // If no content change AND no pending lastRecording -> skip everything, don't lock device
  if (!needsRender && lastRecording == nullptr) {
    return false;
  }

  // ========== Now lock device for rendering/submitting ==========

  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return false;
  }

  auto surface = window->getSurface(context);
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
    displayList.setZoomScale(zoom * baseScale);
    displayList.setContentOffset(baseOffsetX + offsetX, baseOffsetY + offsetY);
    lastZoom = zoom;
    lastOffsetX = offsetX;
    lastOffsetY = offsetY;
    needsRender = true;
  }

  // Clear sizeInvalidated flag
  sizeInvalidated = false;

  // Track if we submitted anything this frame
  bool didSubmit = false;

  // Case 1: No content change BUT have pending lastRecording -> only submit lastRecording
  if (!needsRender) {
    context->submit(std::move(lastRecording));
    window->present(context);
    didSubmit = true;
    device->unlock();
    return didSubmit;
  }

  // Case 2: Content changed -> render new content
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  auto density = static_cast<float>(surface->width()) / static_cast<float>(width);
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), density);

  // Render DisplayList
  displayList.render(surface.get(), false);

  // Delayed one-frame present mode
  auto recording = context->flush();
  if (lastRecording) {
    // Normal delayed mode: submit last frame, save current for next
    context->submit(std::move(lastRecording));
    window->present(context);
    didSubmit = true;
    lastRecording = std::move(recording);
  } else if (recording) {
    // No lastRecording (first frame or after size change): submit current directly
    context->submit(std::move(recording));
    window->present(context);
    didSubmit = true;
  }

  device->unlock();

  // Return true if we submitted or have pending recording
  return didSubmit || lastRecording != nullptr;
}
}  // namespace hello2d

static hello2d::JTGFXView* GetJTGFXView(JNIEnv* env, jobject thiz) {
  return reinterpret_cast<hello2d::JTGFXView*>(
      env->GetLongField(thiz, hello2d::TGFXView_nativePtr));
}

static std::unique_ptr<hello2d::AppHost> CreateAppHost(ANativeWindow* nativeWindow, float density) {
  auto host = std::make_unique<hello2d::AppHost>();

  static const std::string FallbackFontFileNames[] = {"/system/fonts/NotoSansCJK-Regular.ttc",
                                                      "/system/fonts/NotoSansSC-Regular.otf",
                                                      "/system/fonts/DroidSansFallback.ttf"};
  for (auto& fileName : FallbackFontFileNames) {
    auto typeface = tgfx::Typeface::MakeFromPath(fileName);
    if (typeface != nullptr) {
      host->addTypeface("default", std::move(typeface));
      break;
    }
  }
  return host;
}

extern "C" {
JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeRelease(JNIEnv* env, jobject thiz) {
  auto jTGFXView =
      reinterpret_cast<hello2d::JTGFXView*>(env->GetLongField(thiz, hello2d::TGFXView_nativePtr));
  if (jTGFXView != nullptr) {
    delete jTGFXView;
    env->SetLongField(thiz, hello2d::TGFXView_nativePtr, 0);
  }
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_00024Companion_nativeInit(JNIEnv* env,
                                                                                jobject) {
  auto clazz = env->FindClass("org/tgfx/hello2d/TGFXView");
  hello2d::TGFXView_nativePtr = env->GetFieldID(clazz, "nativePtr", "J");
}

JNIEXPORT jlong JNICALL Java_org_tgfx_hello2d_TGFXView_00024Companion_setupFromSurface(
    JNIEnv* env, jobject, jobject surface, jobjectArray imageBytesArray, jbyteArray fontBytes,
    jfloat density) {
  if (surface == nullptr) {
    return 0;
  }

  auto nativeWindow = ANativeWindow_fromSurface(env, surface);
  if (nativeWindow == nullptr) {
    return 0;
  }

  auto window = tgfx::EGLWindow::MakeFrom(nativeWindow);
  if (window == nullptr) {
    ANativeWindow_release(nativeWindow);
    return 0;
  }

  auto appHost = CreateAppHost(nativeWindow, density);
  if (appHost == nullptr) {
    ANativeWindow_release(nativeWindow);
    return 0;
  }

  jsize numArrays = env->GetArrayLength(imageBytesArray);

  for (jsize i = 0; i < numArrays; i++) {
    jbyteArray imageBytes = static_cast<jbyteArray>(env->GetObjectArrayElement(imageBytesArray, i));
    if (imageBytes == nullptr) {
      continue;
    }

    auto bytes = env->GetByteArrayElements(imageBytes, nullptr);
    auto size = static_cast<size_t>(env->GetArrayLength(imageBytes));
    auto data = tgfx::Data::MakeWithCopy(bytes, size);
    auto image = tgfx::Image::MakeFromEncoded(data);
    env->ReleaseByteArrayElements(imageBytes, bytes, 0);

    if (image) {
      const char* imageName = (i == 0) ? "bridge" : "TGFX";
      appHost->addImage(imageName, std::move(image));
    }
  }

  if (fontBytes != nullptr) {
    auto bytes = env->GetByteArrayElements(fontBytes, nullptr);
    auto size = static_cast<size_t>(env->GetArrayLength(fontBytes));
    auto data = tgfx::Data::MakeWithCopy(bytes, size);
    auto typeface = tgfx::Typeface::MakeFromData(data);
    env->ReleaseByteArrayElements(fontBytes, bytes, 0);

    if (typeface) {
      appHost->addTypeface("emoji", std::move(typeface));
    }
  }

  auto jTGFXView = new hello2d::JTGFXView(nativeWindow, std::move(window), std::move(appHost));

  return reinterpret_cast<jlong>(jTGFXView);
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeDraw(JNIEnv* env, jobject thiz,
                                                                 jint drawIndex, jfloat zoom,
                                                                 jfloat offsetX, jfloat offsetY) {
  auto view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }

  view->draw(drawIndex, zoom, offsetX, offsetY);
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_updateSize(JNIEnv* env, jobject thiz) {
  auto view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateSize();
}
}
