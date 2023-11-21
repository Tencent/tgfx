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

#include "JTGFXView.h"
#include "tdraw/Drawer.h"

namespace hello2d {
static jfieldID TGFXView_nativePtr;

void JTGFXView::updateSize() {
  _width = ANativeWindow_getWidth(nativeWindow);
  _height = ANativeWindow_getHeight(nativeWindow);
  appHost->updateScreen(_width, _height, appHost->density());
  surface = nullptr;
}

void JTGFXView::draw(int index) {
  if (surface == nullptr) {
    createSurface();
  }
  if (surface == nullptr) {
    return;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->save();
  auto numDrawers = tdraw::Drawer::Count() - 1;
  index = (index % numDrawers) + 1;
  auto drawer = tdraw::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost.get());
  drawer = tdraw::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost.get());
  canvas->restore();
  surface->flush();
  context->submit();
  window->present(context);
  device->unlock();
}

void JTGFXView::createSurface() {
  if (_width <= 0 || _height <= 0) {
    return;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  surface = window->createSurface(context);
  device->unlock();
}
}

static hello2d::JTGFXView* GetJTGFXView(JNIEnv* env, jobject thiz) {
  return reinterpret_cast<hello2d::JTGFXView*>(env->GetLongField(thiz,
                                                                 hello2d::TGFXView_nativePtr));
}

static std::unique_ptr<tdraw::AppHost> CreateAppHost(ANativeWindow* nativeWindow, float density) {
  auto host = std::make_unique<tdraw::AppHost>();
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  host->updateScreen(width, height, density);
  static const std::string FallbackFontFileNames[] =
      {"/system/fonts/NotoSansCJK-Regular.ttc",
       "/system/fonts/NotoSansSC-Regular.otf",
       "/system/fonts/DroidSansFallback.ttf"};
  for (auto& fileName : FallbackFontFileNames) {
    auto typeface = tgfx::Typeface::MakeFromPath(fileName);
    if (typeface != nullptr) {
      host->addTypeface("default", std::move(typeface));
      break;
    }
  }
  auto typeface = tgfx::Typeface::MakeFromPath("/system/fonts/NotoColorEmoji.ttf");
  if (typeface != nullptr) {
    host->addTypeface("emoji", std::move(typeface));
  }
  return host;
}

extern "C" {
JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_nativeRelease(JNIEnv* env, jobject thiz) {
  auto jTGFXView =
      reinterpret_cast<hello2d::JTGFXView*>(env->GetLongField(thiz, hello2d::TGFXView_nativePtr));
  delete jTGFXView;
}

JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_00024Companion_nativeInit(JNIEnv* env, jobject) {
  auto clazz = env->FindClass("org/tgfx/hello2d/TGFXView");
  hello2d::TGFXView_nativePtr = env->GetFieldID(clazz, "nativePtr", "J");
}

JNIEXPORT jlong JNICALL
Java_org_tgfx_hello2d_TGFXView_00024Companion_setupFromSurface(JNIEnv* env, jobject,
                                                               jobject surface,
                                                               jbyteArray imageBytes,
                                                               jfloat density) {
  if (surface == nullptr) {
    printf("SetupFromSurface() Invalid surface specified.\n");
    return 0;
  }
  auto nativeWindow = ANativeWindow_fromSurface(env, surface);
  auto window = tgfx::EGLWindow::MakeFrom(nativeWindow);
  if (window == nullptr) {
    printf("SetupFromSurface() Invalid surface specified.\n");
    return 0;
  }
  auto bytes = env->GetByteArrayElements(imageBytes, nullptr);
  auto size = env->GetArrayLength(imageBytes);
  auto data = tgfx::Data::MakeWithCopy(bytes, size);
  auto image = tgfx::Image::MakeFromEncoded(data);
  env->ReleaseByteArrayElements(imageBytes, bytes, 0);
  auto appHost = CreateAppHost(nativeWindow, density);
  if (image) {
    appHost->addImage("bridge", std::move(image));
  }
  return reinterpret_cast<jlong>(new hello2d::JTGFXView(nativeWindow,
                                                        std::move(window),
                                                        std::move(appHost)));
}

JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_nativeDraw(JNIEnv* env, jobject thiz, jint drawIndex) {
  auto* view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->draw(drawIndex);
}

JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_updateSize(JNIEnv* env, jobject thiz) {
  auto* view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateSize();
}
}
