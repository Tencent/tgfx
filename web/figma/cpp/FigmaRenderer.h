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
  void registerFonts(const emscripten::val& native_text_font);
  void invalisize();
  void handMessage(std::string message);
  void test();

 private:
  void dispatchMessage(const JsMessage& message);
  void render();
  tgfx::Layer* getDrawingLayer();

  std::string canvas_id_;
  std::shared_ptr<tgfx::Typeface> demo_text_typeface_;
  std::shared_ptr<tgfx::Window> tgfx_window_ = nullptr;
  std::shared_ptr<tgfx::Device> tgfx_device_ = nullptr;
  std::shared_ptr<tgfx::DisplayList> tgfx_display_list_ = nullptr;
  std::shared_ptr<tgfx::Layer> layer = nullptr;
};
