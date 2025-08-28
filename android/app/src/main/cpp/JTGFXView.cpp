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
#include "drawers/Drawer.h"

namespace hello2d {
static jfieldID TGFXView_nativePtr;
bool isFristDraw = true;

void JTGFXView::updateSize() {
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  auto sizeChanged = appHost->updateScreen(width, height, appHost->density());
  if (sizeChanged) {
    window->invalidSize();
  }
}

void JTGFXView::localMarkDirty() {
  appHost->markDirty();
}

bool JTGFXView::draw(int drawIndex, float zoom, float offsetX, float offsetY) {
  if (isFristDraw) {
    isFristDraw = false;
    appHost->markDirty();
  }
  if (!appHost->isDirty()) {
    printf("draw() isDirty() == false.\n");
    return false;
  }
  appHost->resetDirty();

  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return true;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return true;
  }
  auto surface = window->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return true;
  }

  appHost->updateZoomAndOffset(zoom, tgfx::Point(offsetX, offsetY));
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto numDrawers = drawers::Drawer::Count();
  auto index = (drawIndex % numDrawers);
  appHost->draw(canvas, index);
  appHost->draw(canvas, index);
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

static std::unique_ptr<drawers::AppHost> CreateAppHost(ANativeWindow* nativeWindow, float density) {
  auto host = std::make_unique<drawers::AppHost>();
  auto width = ANativeWindow_getWidth(nativeWindow);
  auto height = ANativeWindow_getHeight(nativeWindow);
  host->updateScreen(width, height, density);

  static const std::string FallbackFontFileNames[] = {
      "/system/fonts/NotoSansCJK-Regular.ttc",
      "/system/fonts/NotoSansSC-Regular.otf",
      "/system/fonts/DroidSansFallback.ttf"
  };
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

// 初始化 native 端，传入 Surface、图片数组、emoji 字体数据及屏幕密度
// 返回 jlong 形式的 JTGFXView 指针给 Java 层持有
JNIEXPORT jlong JNICALL Java_org_tgfx_hello2d_TGFXView_00024Companion_setupFromSurface(
    JNIEnv* env, jobject, jobject surface, jobjectArray imageBytesArrays, jbyteArray fontBytes,
    jfloat density) {
  if (surface == nullptr) {
    printf("SetupFromSurface() Invalid surface specified.\n");
    return 0;
  }

  // 将 Java Surface 转成 ANativeWindow 并创建 EGL 窗口
  auto nativeWindow = ANativeWindow_fromSurface(env, surface);
  auto window = tgfx::EGLWindow::MakeFrom(nativeWindow);
  if (window == nullptr) {
    printf("SetupFromSurface() Invalid surface specified.\n");
    return 0;
  }

  // 创建并初始化 AppHost
  auto appHost = CreateAppHost(nativeWindow, density);

  // 解码并添加 Java 层传进来的所有图片
  jsize numArrays = env->GetArrayLength(imageBytesArrays);
  for (jsize i = 0; i < numArrays; i++) {
    jbyteArray imageBytes =
        static_cast<jbyteArray>(env->GetObjectArrayElement(imageBytesArrays, i));
    auto bytes = env->GetByteArrayElements(imageBytes, nullptr);
    auto size = static_cast<size_t>(env->GetArrayLength(imageBytes));
    auto data = tgfx::Data::MakeWithCopy(bytes, size);
    auto image = tgfx::Image::MakeFromEncoded(data);
    env->ReleaseByteArrayElements(imageBytes, bytes, 0);
    if (image) {
      appHost->addImage(i == 0 ? "bridge" : "TGFX", std::move(image));
    }
  }

  // 解码并添加 emoji 字体
  auto bytes = env->GetByteArrayElements(fontBytes, nullptr);
  auto size = static_cast<size_t>(env->GetArrayLength(fontBytes));
  auto data = tgfx::Data::MakeWithCopy(bytes, size);
  auto typeface = tgfx::Typeface::MakeFromData(data);
  env->ReleaseByteArrayElements(fontBytes, bytes, 0);
  if (typeface) {
    appHost->addTypeface("emoji", std::move(typeface));
  }

  // 创建并返回 JTGFXView 实例
  return reinterpret_cast<jlong>(
      new hello2d::JTGFXView(nativeWindow, std::move(window), std::move(appHost)));

}

// Java 层主动触发绘制，传入要展示的 Drawer 索引及缩放/偏移参数
JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_nativeDraw(JNIEnv* env, jobject thiz,
                                                                 jint drawIndex, jfloat zoom,
                                                                 jfloat offsetX, jfloat offsetY) {
                              
                                                                  
  auto* view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->localMarkDirty();
  view->draw(drawIndex, zoom, offsetX, offsetY);
}

// Java 层在 Surface 尺寸变化后通知 native 更新尺寸
JNIEXPORT void JNICALL Java_org_tgfx_hello2d_TGFXView_updateSize(JNIEnv* env, jobject thiz) {
  auto* view = GetJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateSize();
}
}
