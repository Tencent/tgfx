#include <tgfx/layers/DisplayList.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/Layer.h>
#include <memory>
#include <string>
#include "Element.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"

class FigmaRenderer {
 public:
  FigmaRenderer() {
  }
  void initialize(std::string canvasID);
  void invalisize();
  void updateShape();
  void handMessage(std::string message);

 private:
  void dispatchMessage(const JsMessage& message);
  void render();
  tgfx::Layer* getDrawingLayer();

  std::shared_ptr<tgfx::Layer> layer = nullptr;
  std::shared_ptr<tgfx::Window> tgfx_window_ = nullptr;
  std::shared_ptr<tgfx::Device> tgfx_device_ = nullptr;
  std::shared_ptr<tgfx::DisplayList> tgfx_display_list_ = nullptr;
  bool enableBackend = false;
};
