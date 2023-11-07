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


namespace hello2d {
static jfieldID TGFXView_nativePtr;

void JTGFXView::updateSize() {
  _width = ANativeWindow_getWidth(nativeWindow);
  _height = ANativeWindow_getHeight(nativeWindow);
  surface = nullptr;
}

void JTGFXView::draw() {
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
  drawBackground(canvas);
  if (drawCount % 2 == 0) {
    drawShape(canvas);
  } else {
    drawImage(canvas);
  }
  canvas->restore();
  drawCount++;
  surface->flush();
  context->submit();
  window->present(context);
  device->unlock();
}

void JTGFXView::drawBackground(tgfx::Canvas* canvas) {
  tgfx::Paint paint;
  paint.setColor(tgfx::Color{0.8f, 0.8f, 0.8f, 1.f});
  int tileSize = 8 * density;
  for (int y = 0; y < _height; y += tileSize) {
    bool draw = (y / tileSize) % 2 == 1;
    for (int x = 0; x < _width; x += tileSize) {
      if (draw) {
        auto rect =
            tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(tileSize), static_cast<float>(tileSize));
        canvas->drawRect(rect, paint);
      }
      draw = !draw;
    }
  }
}

void JTGFXView::drawShape(tgfx::Canvas* canvas) {
  tgfx::Color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
  tgfx::Color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
  tgfx::Color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
  auto shader = tgfx::Shader::MakeSweepGradient(tgfx::Point::Make(_width / 2, _height / 2), 0, 360,
                                                {cyan, magenta, yellow, cyan}, {});
  tgfx::Paint paint = {};
  paint.setShader(shader);
  auto size = static_cast<int>(256 * density);
  auto rect = tgfx::Rect::MakeXYWH((_width - size) / 2, (_height - size) / 2, size, size);
  tgfx::Path path = {};
  path.addRoundRect(rect, 20 * density, 20 * density);
  canvas->drawPath(path, paint);
}

void JTGFXView::drawImage(tgfx::Canvas* canvas) {
  auto filter = tgfx::ImageFilter::DropShadow(5 * density, 5 * density, 50 * density, 50 * density,
                                              tgfx::Color::Black());
  tgfx::Paint paint = {};
  paint.setImageFilter(filter);
  auto size = static_cast<int>(256 * density);
  auto rect = tgfx::Rect::MakeXYWH((_width - size) / 2, (_height - size) / 2, size, size);
  tgfx::Path path = {};
  path.addRoundRect(rect, 20 * density, 20 * density);
  auto image = tgfx::Image::MakeFromEncoded(imageBytes);
  if (image != nullptr) {
    canvas->drawImage(image, (_width - image->width()) / 2, (_height - image->height()) / 2,
                      &paint);
  }
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

hello2d::JTGFXView* getJTGFXView(JNIEnv* env, jobject thiz) {
  return reinterpret_cast<hello2d::JTGFXView*>(env->GetLongField(thiz,
                                                                 hello2d::TGFXView_nativePtr));
}

extern "C" {
JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_nativeRelease(JNIEnv* env, jobject thiz) {
  auto jTGFXView =
      reinterpret_cast<hello2d::JTGFXView*>(env->GetLongField(thiz, hello2d::TGFXView_nativePtr));
  delete jTGFXView;
}

JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_00024Companion_nativeInit(JNIEnv* env, jobject thiz) {
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
  env->ReleaseByteArrayElements(imageBytes, bytes, 0);
  return reinterpret_cast<jlong>(new hello2d::JTGFXView(nativeWindow,
                                                        std::move(window),
                                                        std::move(data),
                                                        density));
}

JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_nativeDraw(JNIEnv* env, jobject thiz) {
  auto* view = getJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->draw();
}

JNIEXPORT void JNICALL
Java_org_tgfx_hello2d_TGFXView_updateSize(JNIEnv* env, jobject thiz) {
  auto* view = getJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateSize();
}
}
