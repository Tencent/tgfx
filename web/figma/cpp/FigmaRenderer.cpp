#include "FigmaRenderer.h"

void FigmaRenderer::initialize(std::string canvasID) {
  tgfx_window_ = tgfx::WebGLWindow::MakeFrom(canvasID);
}
void FigmaRenderer::invalisize() {
  if (tgfx_window_ == nullptr) {
    return;
  }
  tgfx_window_->invalidSize();
}
void FigmaRenderer::updateShape() {
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