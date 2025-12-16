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
    lastSurfaceWidth = width;
    lastSurfaceHeight = height;
    window->invalidSize();
    lastRecording = nullptr;
  }
}

void JTGFXView::updateDisplayList(int drawIndex) {
  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = (drawIndex % numBuilders);
  if (index != lastDrawIndex || !contentLayer) {
    auto builder = hello2d::LayerBuilder::GetByIndex(index);
    if (builder) {
      contentLayer = builder->buildLayerTree(appHost.get());
      if (contentLayer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(contentLayer);
        applyCenteringTransform();
      }
    }
    lastDrawIndex = index;
  }
}

void JTGFXView::updateDisplayTransform(float zoom, float offsetX, float offsetY) {
  displayList.setZoomScale(zoom);
  displayList.setContentOffset(offsetX, offsetY);
}

void JTGFXView::applyCenteringTransform() {
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0 && contentLayer) {
    hello2d::LayerBuilder::ApplyCenteringTransform(
        contentLayer, static_cast<float>(lastSurfaceWidth), static_cast<float>(lastSurfaceHeight));
  }
}

void JTGFXView::draw() {
  if (!displayList.hasContentChanged() && lastRecording == nullptr) {
    return;
  }

  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }

  auto surface = window->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  auto density = static_cast<float>(surface->width()) / static_cast<float>(width);
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), density);

  displayList.render(surface.get(), false);

  auto recording = context->flush();

  // Delayed one-frame present
  std::swap(lastRecording, recording);

  if (recording) {
    context->submit(std::move(recording));
    window->present(context);
  }

  device->unlock();
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

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeUpdateDisplayList(JNIEnv* env,
                                                                              jobject thiz,
                                                                              jint drawIndex) {
  auto view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateDisplayList(drawIndex);
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeUpdateDisplayTransform(
    JNIEnv* env, jobject thiz, jfloat zoom, jfloat offsetX, jfloat offsetY) {
  auto view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateDisplayTransform(zoom, offsetX, offsetY);
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeDraw(JNIEnv* env, jobject thiz) {
  auto view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->draw();
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeUpdateSize(JNIEnv* env, jobject thiz) {
  auto view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateSize();
}
}
