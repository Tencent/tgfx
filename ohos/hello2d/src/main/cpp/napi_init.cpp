#include "napi/native_api.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include "tgfx/gpu/opengl/egl/EGLWindow.h"
#include "tgfx/core/Point.h"
#include "drawers/AppHost.h"
#include "drawers/Drawer.h"
#include <vector>
#include <cmath>

static float screenDensity = 1.0f;
static std::shared_ptr<drawers::AppHost> appHost = nullptr;
static std::shared_ptr<tgfx::Window> window = nullptr;
static float g_zoom = 1.0f;
static tgfx::Point g_offset{0, 0};
static int g_drawIndex = 0;

struct TouchState {
    bool dragging = false;
    float lastX = 0, lastY = 0;
    bool pinching = false;
    float pinchStartDist = 0.0f;
    float pinchStartZoom = 1.0f;
    tgfx::Point pinchStartOffset{0, 0};
    float pinchCenterX = 0, pinchCenterY = 0;
    int pointerIdA = -1, pointerIdB = -1;
    float lastAX = 0, lastAY = 0, lastBX = 0, lastBY = 0;
    bool needResetPanAfterScale = false;
};
static TouchState touchState;

static std::shared_ptr<drawers::AppHost> CreateAppHost();

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

static void Draw(int index) {
  if (window == nullptr || appHost == nullptr || appHost->width() <= 0 || appHost->height() <= 0) {
    return;
  }
    appHost->updateZoomAndOffset(g_zoom, g_offset);
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    printf("Fail to lock context from the Device.\n");
    return;
  }
  auto surface = window->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->save();
  auto numDrawers = drawers::Drawer::Count() - 1;
  index = (index % numDrawers) + 1;
  auto drawer = drawers::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost.get());
  drawer = drawers::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost.get());
  canvas->restore();
  context->flushAndSubmit();
  window->present(context);
  device->unlock();
}

static napi_value OnDraw(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1] = {nullptr};
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  double value;
  napi_get_value_double(env, args[0], &value);
    g_drawIndex = static_cast<int>(value);
    touchState = TouchState();
    g_zoom = 1.0f;
    g_offset = {0,0};
  Draw(static_cast<int>(value));
  return nullptr;
}

static std::shared_ptr<drawers::AppHost> CreateAppHost() {
  auto appHost = std::make_shared<drawers::AppHost>();
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
  }
}

static void OnSurfaceChangedCB(OH_NativeXComponent* component, void* nativeWindow) {
  UpdateSize(component, nativeWindow);
}

static void OnSurfaceDestroyedCB(OH_NativeXComponent*, void*) {
  window = nullptr;
}

static void DispatchTouchEventCB(OH_NativeXComponent* component, void* windowRaw)
{
    OH_NativeXComponent_TouchEvent touchEvent;
    if (OH_NativeXComponent_GetTouchEvent(component, windowRaw, &touchEvent) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return;
    }
    unsigned int pointerCount = touchEvent.numPoints;
    if (pointerCount == 2) {
        float x1 = touchEvent.touchPoints[0].x, y1 = touchEvent.touchPoints[0].y;
        float x2 = touchEvent.touchPoints[1].x, y2 = touchEvent.touchPoints[1].y;
        float centerX = (x1 + x2) / 2.0f;
        float centerY = (y1 + y2) / 2.0f;
        float dist = std::hypot(x2 - x1, y2 - y1);
        if (!touchState.pinching
            || touchEvent.type == OH_NativeXComponent_TouchEventType::OH_NATIVEXCOMPONENT_DOWN) {
            touchState.pinching = true;
            touchState.pinchStartDist = dist;
            touchState.pinchStartZoom = g_zoom;
            touchState.pinchStartOffset = g_offset;
            touchState.pinchCenterX = centerX;
            touchState.pinchCenterY = centerY;
            touchState.pointerIdA = touchEvent.touchPoints[0].id;
            touchState.pointerIdB = touchEvent.touchPoints[1].id;
        }
        else if (touchEvent.type == OH_NativeXComponent_TouchEventType::OH_NATIVEXCOMPONENT_MOVE) {
            if (touchState.pinching) {
                float scale = dist / touchState.pinchStartDist;
                float zoomNew = std::max(0.01f, std::min(100.0f, touchState.pinchStartZoom * scale));
                float ox = (touchState.pinchStartOffset.x - touchState.pinchCenterX) * (zoomNew / touchState.pinchStartZoom) + touchState.pinchCenterX;
                float oy = (touchState.pinchStartOffset.y - touchState.pinchCenterY) * (zoomNew / touchState.pinchStartZoom) + touchState.pinchCenterY;
                g_zoom = zoomNew;
                g_offset.x = ox;
                g_offset.y = oy;
                Draw(g_drawIndex);
            }
        }
        else if (touchEvent.type == OH_NativeXComponent_TouchEventType::OH_NATIVEXCOMPONENT_UP) {
            touchState.pinching = false;
            touchState.needResetPanAfterScale = true;
        }
        return;
    }
    if (pointerCount == 1) {
        float x = touchEvent.touchPoints[0].x;
        float y = touchEvent.touchPoints[0].y;
        if (touchState.pinching) {
            if (touchEvent.type == OH_NativeXComponent_TouchEventType::OH_NATIVEXCOMPONENT_DOWN) {
                touchState.pinching = false;
            } else {
                return;
            }
        }
        if (touchEvent.type == OH_NativeXComponent_TouchEventType::OH_NATIVEXCOMPONENT_DOWN) {
            touchState.dragging = true;
            touchState.lastX = x;
            touchState.lastY = y;
            if (touchState.needResetPanAfterScale) {
                touchState.lastX = x;
                touchState.lastY = y;
                touchState.needResetPanAfterScale = false;
            }
        } else if (touchEvent.type == OH_NativeXComponent_TouchEventType::OH_NATIVEXCOMPONENT_MOVE) {
            if (touchState.needResetPanAfterScale) {
                touchState.lastX = x;
                touchState.lastY = y;
                touchState.needResetPanAfterScale = false;
                return;
            }
            if (touchState.dragging) {
                float dx = x - touchState.lastX;
                float dy = y - touchState.lastY;
                g_offset.x += dx;
                g_offset.y += dy;
                touchState.lastX = x;
                touchState.lastY = y;
                Draw(g_drawIndex);
            }
        }
    }
}

static void OnSurfaceCreatedCB(OH_NativeXComponent* component, void* nativeWindow) {
  UpdateSize(component, nativeWindow);
  window = tgfx::EGLWindow::MakeFrom(reinterpret_cast<EGLNativeWindowType>(nativeWindow));
  if (window == nullptr) {
    printf("OnSurfaceCreatedCB() Invalid surface specified.\n");
    return;
  }
  Draw(0);
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
      {"draw", nullptr, OnDraw, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"updateDensity", nullptr, OnUpdateDensity, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"addImageFromEncoded", nullptr, AddImageFromEncoded, nullptr, nullptr, nullptr, napi_default,
       nullptr}};
  napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
  RegisterCallback(env, exports);
  return exports;
}
EXTERN_C_END

static napi_module demoModule = {1, 0, nullptr, Init, "hello2d", ((void*)0), {0}};

extern "C" __attribute__((constructor)) void RegisterHello2dModule(void) {
  napi_module_register(&demoModule);
}