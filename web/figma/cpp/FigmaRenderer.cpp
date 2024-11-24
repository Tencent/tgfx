#include "FigmaRenderer.h"
#include <emscripten/console.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include "Element.h"
#include "LayerUtils.h"
#include "MessageParser.h"
#include "utils.h"

#include <fstream>


void printFileInfo(const std::string& filePath) {


    // 打开文件
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to open file " << filePath << std::endl;
        return;
    }
    file.close();
}


void FigmaRenderer::initialize(std::string canvasID) {
  emscripten_log(EM_LOG_CONSOLE, "initialize called， canvasID is %s", canvasID.c_str());
  tgfx_window_ = tgfx::WebGLWindow::MakeFrom(canvasID);
  canvas_id_ = canvasID;
}

std::shared_ptr<tgfx::Data> getDataFromEmscripten(const emscripten::val& native_font) {
  if (native_font.isUndefined()) {
    return nullptr;
  }
  unsigned int length = native_font["length"].as<unsigned int>();
  if (length == 0) {
    return nullptr;
  }
  auto buffer = new (std::nothrow) uint8_t[length];
  if (buffer) {
    auto memory = emscripten::val::module_property("HEAPU8")["buffer"];
    auto memoryView = native_font["constructor"].new_(memory, reinterpret_cast<uintptr_t>(buffer), length);
    memoryView.call<void>("set", native_font);
    return tgfx::Data::MakeAdopted(buffer, length, tgfx::Data::DeleteProc);
  }
  return nullptr;
}

void FigmaRenderer::registerFonts(const emscripten::val& native_text_font) {
  auto text_font_data = getDataFromEmscripten(native_text_font);
  if (text_font_data) {
    this->demo_text_typeface_ = tgfx::Typeface::MakeFromData(text_font_data, 0);
    LayerUtils::SetTypeface(demo_text_typeface_);
  }
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
