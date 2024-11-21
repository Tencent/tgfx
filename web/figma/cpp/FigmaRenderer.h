#include <memory>
#include <string>
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"
#include <tgfx/layers/DisplayList.h>
#include <tgfx/layers/ShapeLayer.h>

class FigmaRenderer {
 public:
  FigmaRenderer() {
  }
  void initialize(std::string canvasID);
  void invalisize();
  void updateShape();
  void handMessage(std::string message);

 private:
  std::shared_ptr<tgfx::Window> tgfx_window_;
  std::shared_ptr<tgfx::Device> tgfx_device_;
  std::shared_ptr<tgfx::DisplayList> tgfx_display_list_;
};
