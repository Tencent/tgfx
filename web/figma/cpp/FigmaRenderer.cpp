#include "FigmaRenderer.h"
#include <emscripten/console.h>

#include <iostream>

void FigmaRenderer::initialize(std::string canvasID) {
  emscripten_log(EM_LOG_CONSOLE, "initialize called， canvasID is %s", canvasID.c_str());
  tgfx_window_ = tgfx::WebGLWindow::MakeFrom(canvasID);
}

void FigmaRenderer::invalisize() {
  emscripten_log(EM_LOG_CONSOLE, "invalisize called");
  if (tgfx_window_ == nullptr) {
    return;
  }
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

  if (tgfx_display_list_ == nullptr) {
    tgfx_display_list_ = std::make_shared<tgfx::DisplayList>();
  }

  auto shapeLayer = tgfx::ShapeLayer::Make();
  tgfx::Path path;
  path.addRect({100, 100, 200, 200});
  shapeLayer->setPath(path);
  std::cout << "ffjiefan：：make shape ok" << std::endl;

  auto fillStyle = tgfx::SolidColor::Make(tgfx::Color::Red());
  shapeLayer->setFillStyle(fillStyle);

  tgfx_display_list_->root()->addChild(shapeLayer);

  auto context = tgfx_device_->lockContext();
  if (context == nullptr) return;
  auto surface = tgfx_window_->getSurface(context);
  if (surface == nullptr) {
    tgfx_device_->unlock();
    return;
  }
  tgfx_display_list_->render(surface.get());

  context->flushAndSubmit();
  tgfx_window_->present(context);

  std::cout << "ffjiefan：：updateShape done" << std::endl;

  tgfx_device_->unlock();
}