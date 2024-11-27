#include "FigmaRenderer.h"
#include <emscripten/console.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "Element.h"
#include "LayerUtils.h"
#include "MessageParser.h"
#include "utils.h"

void FigmaRenderer::initialize(std::string canvasID) {
  logInfo("initialize called， canvasID is " + canvasID);
  tgfx_window_ = tgfx::WebGLWindow::MakeFrom(canvasID);
  canvas_id_ = canvasID;
}

void FigmaRenderer::registerFonts(const emscripten::val& native_text_font) {
  auto text_font_data = getDataFromEmscripten(native_text_font);
  if (text_font_data) {
    this->demo_text_typeface_ = tgfx::Typeface::MakeFromData(text_font_data, 0);
    LayerUtils::SetTypeface(demo_text_typeface_);
  }
}

void FigmaRenderer::invalisize() {
  logInfo("invalisize called");
  if (tgfx_window_ == nullptr) {
    return;
  }
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvas_id_.c_str(), &width, &height);
  // 打印宽高
  logInfo("FigmaRenderer::invalisize width is " + std::to_string(width) + ", height is " +
          std::to_string(height));
  tgfx_window_->invalidSize();
}

void FigmaRenderer::handMessage(std::string message) {
  logInfo("FigmaRenderer::handMessage called, message is " + message);

  JsMessage jsMessage;
  if (MessageParser::parseMessage(message, jsMessage)) {
    logInfo("FigmaRenderer::handMessage jsMessage.action is " + jsMessage.action);
    dispatchMessage(jsMessage);
    render();
  } else {
    logError("FigmaRenderer::handMessage 解析消息失败");
  }
}

void FigmaRenderer::dispatchMessage(const JsMessage& message) {

  const auto [action, canvasRect, viewBox, elements] = message;
  if (action == "enableBackend") {
    getDrawingLayer()->setAlpha(1);
    LayerUtils::UpdateCanvasMatrix(getDrawingLayer(), canvasRect, viewBox);
    LayerUtils::UpdateShape(getDrawingLayer(), elements);
  } else if (action == "canvasPan") {
    LayerUtils::UpdateCanvasMatrix(getDrawingLayer(), canvasRect, viewBox);
  } else if (action == "add" || action == "update") {
    LayerUtils::UpdateCanvasMatrix(getDrawingLayer(), canvasRect, viewBox);
    LayerUtils::UpdateShape(getDrawingLayer(), elements);
  } else if (action == "move") {
    LayerUtils::MoveShape(getDrawingLayer(), elements);
  } else if (action == "disableBackend") {
    getDrawingLayer()->setAlpha(0);
  } else if (action == "viewRectChanged") {
    if (tgfx_window_ != nullptr) {
      logInfo("FigmaRenderer::dispatchMessage viewRectChanged");
      tgfx_window_->invalidSize();
    }
  } else {
    logError("FigmaRenderer::handMessage 未知操作：" + action);
  }
}

void FigmaRenderer::render() {
  logInfo("FigmaRenderer::render called");
  if (tgfx_window_ == nullptr) return;
  if (tgfx_device_ == nullptr) {
    tgfx_device_ = tgfx_window_->getDevice();
  }
  if (tgfx_device_ == nullptr) return;

  if (tgfx_display_list_ == nullptr) return;
  auto context = tgfx_device_->lockContext();
  if (context == nullptr) return;
  auto surface = tgfx_window_->getSurface(context);
  if (surface == nullptr) {
    tgfx_device_->unlock();
    return;
  }

  logInfo("FigmaRenderer::render begin");

  tgfx_display_list_->render(surface.get());

  // 调试，打印layer的区域 begin
  // printLayerBounds(tgfx_display_list_->root(), 0);
  // 调试，打印layer的区域 end
  context->flushAndSubmit();
  tgfx_window_->present(context);

  logInfo("FigmaRenderer::render done");

  tgfx_device_->unlock();
}

tgfx::Layer* FigmaRenderer::getDrawingLayer() {
  if (layer == nullptr || tgfx_display_list_ == nullptr) {
    layer = tgfx::Layer::Make();
    tgfx_display_list_ = std::make_shared<tgfx::DisplayList>();
    tgfx_display_list_->root()->addChild(layer);
  }
  return layer.get();
}

void FigmaRenderer::test() {
  logInfo("test called");
  logInfo("FigmaRenderer::test begin");
  if (tgfx_window_ == nullptr) return;
  if (tgfx_device_ == nullptr) {
    tgfx_device_ = tgfx_window_->getDevice();
  }
  if (tgfx_device_ == nullptr) return;

  auto context = tgfx_device_->lockContext();
  if (context == nullptr) return;
  auto surface = tgfx_window_->getSurface(context);
  if (surface == nullptr) {
    tgfx_device_->unlock();
    return;
  }
  auto paint = tgfx::Paint();
  paint.setColor(tgfx::Color::Red());
  surface->getCanvas()->drawRect({490, 390, 510, 410}, paint);

  // std::string filePath = "assets/font/NotoColorEmoji.ttf";
  // printFileInfo(filePath);

  // auto typeface = tgfx::Typeface::MakeFromName("Arial", "Regular");
  // auto typeface = tgfx::Typeface::MakeFromPath("assets/font/NotoColorEmoji.ttf");
  // if (!typeface) {
  //   std::cerr << "Error: Failed to load typeface from /assets/font/NotoColorEmoji.ttf" << std::endl;
  // }

  std::string text = " 啦啦啦啦啦啦 🤡👻🐠🤩😃🤪🙈🙊🐒";
  const tgfx::Font font(demo_text_typeface_, 48);

  surface->getCanvas()->drawSimpleText(text, 100, 100, font, paint);

  context->flushAndSubmit();
  tgfx_window_->present(context);

  logInfo("FigmaRenderer::test done");

  tgfx_device_->unlock();
}

void FigmaRenderer::logInfo(const std::string& message) const {
  if (enable_info_logging_) {
    std::cout << message << std::endl;
  }
}

void FigmaRenderer::logError(const std::string& message)const {
  if (enable_error_logging_) {
    std::cerr << message << std::endl;
  }
}

