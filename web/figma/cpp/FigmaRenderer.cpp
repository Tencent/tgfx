#include "FigmaRenderer.h"
#include <emscripten/console.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include "Element.h"
#include "LayerUtils.h"
#include "MessageParser.h"
#include "utils.h"

void FigmaRenderer::initialize(std::string canvasID) {
  emscripten_log(EM_LOG_CONSOLE, "initialize called， canvasID is %s", canvasID.c_str());
  tgfx_window_ = tgfx::WebGLWindow::MakeFrom(canvasID);
  canvas_id_ = canvasID;
}

void FigmaRenderer::invalisize() {
  emscripten_log(EM_LOG_CONSOLE, "invalisize called");
  if (tgfx_window_ == nullptr) {
    return;
  }
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvas_id_.c_str(), &width, &height);
  // 打印宽高
  std::cout << "ffjiefan：：invalisize width is " << width << ", height is " << height << std::endl;
  tgfx_window_->invalidSize();
}

void FigmaRenderer::updateShape() {
  emscripten_log(EM_LOG_CONSOLE, "updateShape called");
  // 打印log
  std::cout << "ffjiefan：：updateShape begin " << std::endl;
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

  context->flushAndSubmit();
  tgfx_window_->present(context);

  std::cout << "ffjiefan：：updateShape done" << std::endl;

  tgfx_device_->unlock();
}

void FigmaRenderer::handMessage(std::string message) {
  // 打印log
  std::cout << "ffjiefan：：handMessage called, message is " << message << std::endl;

  JsMessage jsMessage;
  if (MessageParser::parseMessage(message, jsMessage)) {
    // 处理解析后的 jsMessage
    std::cout << "ffjiefan：：handMessage jsMessage.action is " << jsMessage.action << std::endl;
    dispatchMessage(jsMessage);
    render();
  } else {
    std::cerr << "ffjiefan：：handMessage 解析消息失败" << std::endl;
  }
}

void FigmaRenderer::render() {
  // 打印log
  std::cout << "ffjiefan：：render calld " << std::endl;
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

  std::cout << "ffjiefan：：render begin" << std::endl;

  tgfx_display_list_->render(surface.get());

  // 调试，打印layer的区域 begin
  // printLayerBounds(tgfx_display_list_->root(), 0);
  // 调试，打印layer的区域 end
  context->flushAndSubmit();
  tgfx_window_->present(context);

  std::cout << "ffjiefan：：render done" << std::endl;

  tgfx_device_->unlock();
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
      // 打印log
      std::cout << "ffjiefan：：dispatchMessage viewRectChanged" << std::endl;
      tgfx_window_->invalidSize();
    }
  } else {
    // 打印log
    std::cerr << "ffjiefan：：handMessage 未知操作：" << action << std::endl;
  }
}

tgfx::Layer* FigmaRenderer::getDrawingLayer() {
  if (layer == nullptr || tgfx_display_list_ == nullptr) {
    layer = tgfx::Layer::Make();
    tgfx_display_list_ = std::make_shared<tgfx::DisplayList>();
    tgfx_display_list_->root()->addChild(layer);
  }
  return layer.get();
}
