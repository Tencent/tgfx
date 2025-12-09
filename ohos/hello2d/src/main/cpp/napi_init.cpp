#include "napi/native_api.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include "tgfx/gpu/opengl/egl/EGLWindow.h"
#include "tgfx/gpu/Recording.h"
#include "hello2d/AppHost.h"
#include "hello2d/LayerBuilder.h"
#include "DisplayLink.h"
#include "tgfx/layers/DisplayList.h"

static float screenDensity = 1.0f;
static double drawIndex = 0;
static double zoomScale = 1;
static double contentOffsetX = 0;
static double contentOffsetY = 0;
static std::shared_ptr<hello2d::AppHost> appHost = nullptr;
static std::shared_ptr<tgfx::Window> window = nullptr;
static std::shared_ptr<DisplayLink> displayLink = nullptr;
static tgfx::DisplayList displayList;
static std::shared_ptr<tgfx::Layer> contentLayer = nullptr;
static std::unique_ptr<tgfx::Recording> lastRecording = nullptr;
static int lastDrawIndex = -1;
// Cached surface size for calculating base scale without locking device
static int lastSurfaceWidth = 0;
static int lastSurfaceHeight = 0;
// Flag to force render when size changes
static bool sizeInvalidated = false;
// Cached draw parameters for recalculating transform when they change
static float lastZoomScale = 1.0f;
static float lastOffsetX = 0.0f;
static float lastOffsetY = 0.0f;

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
  if (!appHost) {
    return false;
  }

  if (window == nullptr) {
    return false;
  }

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
    bool zoomChanged = (zoom != lastZoomScale);
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
      lastZoomScale = zoom;
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
    lastZoomScale = zoom;
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
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), screenDensity);

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


static napi_value UpdateDrawParams(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4] = {nullptr};
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  napi_get_value_double(env, args[0], &drawIndex);
  napi_get_value_double(env, args[1], &zoomScale);
  napi_get_value_double(env, args[2], &contentOffsetX);
  napi_get_value_double(env, args[3], &contentOffsetY);

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
  displayList.setRenderMode(tgfx::RenderMode::Tiled);
  displayList.setAllowZoomBlur(true);
  displayList.setMaxTileCount(512);
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
  if (window != nullptr) {
    window->invalidSize();
    // Clear lastRecording when size changes, as it was created for the old surface size
    lastRecording = nullptr;
    // Mark size as invalidated to force render next frame
    sizeInvalidated = true;
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
  lastRecording = nullptr;
}

static void DispatchTouchEventCB(OH_NativeXComponent*, void*) {
}

static void OnSurfaceCreatedCB(OH_NativeXComponent* component, void* nativeWindow) {
  UpdateSize(component, nativeWindow);
  window = tgfx::EGLWindow::MakeFrom(reinterpret_cast<EGLNativeWindowType>(nativeWindow));
  if (window == nullptr) {
    return;
  }
  if (displayLink) {
    displayLink->start();
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
