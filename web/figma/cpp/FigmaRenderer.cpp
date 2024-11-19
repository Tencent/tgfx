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
  std::cout << "ffjiefan：：updateShape" << std::endl;
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
  auto canvas = surface->getCanvas();
  auto image = tgfx::Image::MakeFromFile(
      "/Users/fanjie/Downloads/Test_Images/sam-quek-7zNkkynKVcc-unsplash.jpg");

  canvas->drawImage(image);

  context->flushAndSubmit();
  tgfx_window_->present(context);

  tgfx_device_->unlock();
}