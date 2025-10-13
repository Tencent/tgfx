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
#include "hello2d/SampleBuilder.h"

namespace hello2d {
static jfieldID TGFXView_nativePtr;

void JTGFXView::updateSize() {
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  auto sizeChanged = appHost->updateScreen(width, height, appHost->density());
  if (sizeChanged) {
    window->invalidSize();
  }
}

void JTGFXView::markDirty() {
  appHost->markDirty();
}

bool JTGFXView::draw(int drawIndex, float zoom, float offsetX, float offsetY) {
  if (!appHost->isDirty()) {
    return false;
  }
  appHost->resetDirty();

  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return false;
  }
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

  appHost->updateZoomAndOffset(zoom, tgfx::Point(offsetX, offsetY));
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto numDrawers = hello2d::SampleBuilder::Count();
  auto index = (drawIndex % numDrawers);
  appHost->draw(canvas, index, true);
  context->flushAndSubmit();
  window->present(context);
  device->unlock();
  return true;
}
}  // namespace hello2d

static hello2d::JTGFXView* GetJTGFXView(JNIEnv* env, jobject thiz) {
  return reinterpret_cast<hello2d::JTGFXView*>(
      env->GetLongField(thiz, hello2d::TGFXView_nativePtr));
}

static std::unique_ptr<hello2d::AppHost> CreateAppHost(ANativeWindow* nativeWindow, float density) {
  auto host = std::make_unique<hello2d::AppHost>();
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  host->updateScreen(width, height, density);

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
  delete jTGFXView;
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
  auto window = tgfx::EGLWindow::MakeFrom(nativeWindow);
  if (window == nullptr) {
    return 0;
  }

  auto appHost = CreateAppHost(nativeWindow, density);

  jsize numArrays = env->GetArrayLength(imageBytesArray);
  for (jsize i = 0; i < numArrays; i++) {
    jbyteArray imageBytes = static_cast<jbyteArray>(env->GetObjectArrayElement(imageBytesArray, i));
    auto bytes = env->GetByteArrayElements(imageBytes, nullptr);
    auto size = static_cast<size_t>(env->GetArrayLength(imageBytes));
    auto data = tgfx::Data::MakeWithCopy(bytes, size);
    auto image = tgfx::Image::MakeFromEncoded(data);
    env->ReleaseByteArrayElements(imageBytes, bytes, 0);
    if (image) {
      appHost->addImage(i == 0 ? "bridge" : "TGFX", std::move(image));
    }
  }

  auto bytes = env->GetByteArrayElements(fontBytes, nullptr);
  auto size = static_cast<size_t>(env->GetArrayLength(fontBytes));
  auto data = tgfx::Data::MakeWithCopy(bytes, size);
  auto typeface = tgfx::Typeface::MakeFromData(data);
  env->ReleaseByteArrayElements(fontBytes, bytes, 0);
  if (typeface) {
    appHost->addTypeface("emoji", std::move(typeface));
  }

  return reinterpret_cast<jlong>(
      new hello2d::JTGFXView(nativeWindow, std::move(window), std::move(appHost)));
}

JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeDraw(JNIEnv* env, jobject thiz,
                                                                 jint drawIndex, jfloat zoom,
                                                                 jfloat offsetX, jfloat offsetY) {

  auto view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->markDirty();
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
