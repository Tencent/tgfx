#include <jni.h>
#include <string>
#include <jni.h>
#include <android/native_window_jni.h>
#include "tgfx/gpu/Window.h"
#include "tgfx/opengl/egl/EGLWindow.h"

namespace tgfxdemo {
static jfieldID TGFXView_nativePtr;

class JTGFXView {
 public:
  explicit JTGFXView(ANativeWindow* nativeWindow, std::shared_ptr<tgfx::Window> window)
      : nativeWindow(nativeWindow), window(std::move(window)) {
    updateSize();
  }

  ~JTGFXView() {
    ANativeWindow_release(nativeWindow);
  }

  void updateSize() {
    _width = ANativeWindow_getWidth(nativeWindow);
    _height = ANativeWindow_getHeight(nativeWindow);
    surface = nullptr;
  }

  void draw() {
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
    tgfx::Paint paint;
    paint.setColor(tgfx::Color{0.8f, 0.8f, 0.8f, 1.f});
    if (drawCount % 2 == 0) {
      auto rect = tgfx::Rect::MakeXYWH(20, 20, 100, 100);
      canvas->drawRect(rect, paint);
    } else {
      int tileSize = 8;
      for (int y = 0; y < _height; y += tileSize) {
        bool draw = (y / tileSize) % 2 == 1;
        for (int x = 0; x < _width; x += tileSize) {
          if (draw) {
            auto rect = tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(tileSize), static_cast<float>(tileSize));
            canvas->drawRect(rect, paint);
          }
          draw = !draw;
        }
      }
    }
    surface->flush();
    context->submit();
    window->present(context);
    device->unlock();
    drawCount++;
  }

 private:
  void createSurface() {
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

  ANativeWindow* nativeWindow = nullptr;
  std::shared_ptr<tgfx::Window> window;
  std::shared_ptr<tgfx::Surface> surface;
  int _width = 0;
  int _height = 0;
  int drawCount = 0;
};
}

tgfxdemo::JTGFXView* getJTGFXView(JNIEnv* env, jobject thiz) {
  return reinterpret_cast<tgfxdemo::JTGFXView*>(env->GetLongField(thiz,
                                                                  tgfxdemo::TGFXView_nativePtr));
}

extern "C" {
JNIEXPORT void JNICALL
Java_io_pag_tgfxdemo_TGFXView_nativeRelease(JNIEnv* env, jobject thiz) {
  auto jTGFXView =
      reinterpret_cast<tgfxdemo::JTGFXView*>(env->GetLongField(thiz, tgfxdemo::TGFXView_nativePtr));
  delete jTGFXView;
}

JNIEXPORT void JNICALL
Java_io_pag_tgfxdemo_TGFXView_00024Companion_nativeInit(JNIEnv* env, jobject thiz) {
  auto clazz = env->FindClass("io/pag/tgfxdemo/TGFXView");
  tgfxdemo::TGFXView_nativePtr = env->GetFieldID(clazz, "nativePtr", "J");
}

JNIEXPORT jlong JNICALL
Java_io_pag_tgfxdemo_TGFXView_00024Companion_setupFromSurface(JNIEnv* env, jobject,
                                                              jobject surface) {

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
  return reinterpret_cast<jlong>(new tgfxdemo::JTGFXView(nativeWindow, std::move(window)));
}

JNIEXPORT void JNICALL
Java_io_pag_tgfxdemo_TGFXView_nativeDraw(JNIEnv* env, jobject thiz) {
  auto* view = getJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->draw();
}

JNIEXPORT void JNICALL
Java_io_pag_tgfxdemo_TGFXView_updateSize(JNIEnv* env, jobject thiz) {
  auto* view = getJTGFXView(env, thiz);
  if (view == nullptr) {
    return;
  }
  view->updateSize();
}
}
