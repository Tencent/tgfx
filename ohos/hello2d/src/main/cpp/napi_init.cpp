#include "napi/native_api.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include "tgfx/gpu/opengl/egl/EGLWindow.h"
#include "hello2d/AppHost.h"
#include "hello2d/LayerBuilder.h"
#include "DisplayLink.h"

static float screenDensity = 1.0f;
static double drawIndex = 0;
static double zoomScale = 1;
static double contentOffsetX = 0;
static double contentOffsetY = 0;
static std::shared_ptr<hello2d::AppHost> appHost = nullptr;
static std::shared_ptr<tgfx::Window> window = nullptr;
static std::shared_ptr<DisplayLink> displayLink = nullptr;

static std::shared_ptr<hello2d::AppHost> CreateAppHost();

static napi_value OnUpdateDensity(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1] = {nullptr};
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  double value;
  napi_get_value_double(env, args[0], &value);
  screenDensity = static_cast<float>(value);
  return nullptr;
}


static napi_value AddImageFromEncoded(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2] = {nullptr};
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  size_t strSize;
  char srcBuf[2048];
  napi_get_value_string_utf8(env, args[0], srcBuf, sizeof(srcBuf), &strSize);
  std::string name = srcBuf;
  size_t length;
  void* data;
  auto code = napi_get_arraybuffer_info(env, args[1], &data, &length);
  if (code != napi_ok) {
    return nullptr;
  }
  auto tgfxData = tgfx::Data::MakeWithCopy(data, length);
  if (appHost == nullptr) {
    appHost = CreateAppHost();
  }
  appHost->addImage(name, tgfx::Image::MakeFromEncoded(tgfxData));
  return nullptr;
}

static bool Draw(int drawIndex, float zoom = 1.0f, float offsetX = 0.0f, float offsetY = 0.0f) {
  if (!appHost || !appHost->isDirty()) {
    return false;
  }
  appHost->resetDirty();

  if (window == nullptr || appHost->width() <= 0 || appHost->height() <= 0) {
    return true;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    printf("Fail to lock context from the Device.\n");
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

  auto index = (drawIndex % hello2d::LayerBuilder::Count());
  bool isNeedBackground = true;
  appHost->draw(canvas, index, isNeedBackground);
  context->flushAndSubmit();
  window->present(context);
  device->unlock();

  return true;
}


static napi_value UpdateDrawParams(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4] = {nullptr};
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  napi_get_value_double(env, args[0], &drawIndex);
  napi_get_value_double(env, args[1], &zoomScale);
  napi_get_value_double(env, args[2], &contentOffsetX);
  napi_get_value_double(env, args[3], &contentOffsetY);

  if (appHost) {
      appHost->markDirty();
  }

  if (displayLink) {
      displayLink->start();
  }
  return nullptr;
}


static napi_value StartDrawLoop(napi_env, napi_callback_info) {
    if (displayLink == nullptr) {
        displayLink = std::make_shared<DisplayLink>([&]() {
            bool needsRedraw = Draw(static_cast<int>(drawIndex), static_cast<float>(zoomScale),
                 static_cast<float>(contentOffsetX), static_cast<float>(contentOffsetY));

            if (!needsRedraw) {
                displayLink->stop();
            }
        });
    }
    displayLink->start();
    return nullptr;
}


static napi_value StopDrawLoop(napi_env, napi_callback_info) {
    if (displayLink != nullptr) {
        displayLink->stop();
    }
    return nullptr;
}


static std::shared_ptr<hello2d::AppHost> CreateAppHost() {
  auto appHost = std::make_shared<hello2d::AppHost>();
  static const std::string FallbackFontFileNames[] = {"/system/fonts/HarmonyOS_Sans.ttf",
                                                      "/system/fonts/HarmonyOS_Sans_SC.ttf",
                                                      "/system/fonts/HarmonyOS_Sans_TC.ttf"};
  for (auto& fileName : FallbackFontFileNames) {
    auto typeface = tgfx::Typeface::MakeFromPath(fileName);
    if (typeface != nullptr) {
      appHost->addTypeface("default", std::move(typeface));
      break;
    }
  }
  auto typeface = tgfx::Typeface::MakeFromPath("/system/fonts/HMOSColorEmojiCompat.ttf");
  if (typeface != nullptr) {
    appHost->addTypeface("emoji", std::move(typeface));
  }
  return appHost;
}


static void UpdateSize(OH_NativeXComponent* component, void* nativeWindow) {
  uint64_t width;
  uint64_t height;
  int32_t ret = OH_NativeXComponent_GetXComponentSize(component, nativeWindow, &width, &height);
  if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
    return;
  }
  if (appHost == nullptr) {
    appHost = CreateAppHost();
  }
  appHost->updateScreen(static_cast<int>(width), static_cast<int>(height), screenDensity);
  if (window != nullptr) {
    window->invalidSize();
    appHost->markDirty();
    if (displayLink) {
        displayLink->start();
    }
  }
}

static void OnSurfaceChangedCB(OH_NativeXComponent* component, void* nativeWindow) {
  UpdateSize(component, nativeWindow);
}

static void OnSurfaceDestroyedCB(OH_NativeXComponent*, void*) {
  window = nullptr;
  displayLink = nullptr;
}

static void DispatchTouchEventCB(OH_NativeXComponent*, void*) {
}

static void OnSurfaceCreatedCB(OH_NativeXComponent* component, void* nativeWindow) {
  UpdateSize(component, nativeWindow);
  window = tgfx::EGLWindow::MakeFrom(reinterpret_cast<EGLNativeWindowType>(nativeWindow));
  if (window == nullptr) {
    printf("OnSurfaceCreatedCB() Invalid surface specified.\n");
    return;
  }
  if (appHost) {
    appHost->markDirty();
    if (displayLink) {
        displayLink->start();
    }
  }
}

static void RegisterCallback(napi_env env, napi_value exports) {
  napi_status status;
  napi_value exportInstance = nullptr;
  OH_NativeXComponent* nativeXComponent = nullptr;
  status = napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance);
  if (status != napi_ok) {
    return;
  }
  status = napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&nativeXComponent));
  if (status != napi_ok) {
    return;
  }
  static OH_NativeXComponent_Callback callback;
  callback.OnSurfaceCreated = OnSurfaceCreatedCB;
  callback.OnSurfaceChanged = OnSurfaceChangedCB;
  callback.OnSurfaceDestroyed = OnSurfaceDestroyedCB;
  callback.DispatchTouchEvent = DispatchTouchEventCB;
  OH_NativeXComponent_RegisterCallback(nativeXComponent, &callback);
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor desc[] = {
      {"startDrawLoop", nullptr, StartDrawLoop, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"updateDrawParams", nullptr, UpdateDrawParams, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"stopDrawLoop", nullptr, StopDrawLoop, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"updateDensity", nullptr, OnUpdateDensity, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"addImageFromEncoded", nullptr, AddImageFromEncoded, nullptr, nullptr, nullptr, napi_default, nullptr},
  };
  napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
  RegisterCallback(env, exports);
  return exports;
}
EXTERN_C_END

static napi_module demoModule = {1, 0, nullptr, Init, "hello2d", ((void*)0), {0}};

extern "C" __attribute__((constructor)) void RegisterHello2dModule(void) {
  napi_module_register(&demoModule);
}
