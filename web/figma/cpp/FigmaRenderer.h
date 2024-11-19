#include <memory>
#include <string>
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"

class FigmaRenderer {
 public:
  FigmaRenderer() {
  }
  void initialize(std::string canvasID);
  void invalisize();
  void updateShape();

 private:
  std::shared_ptr<tgfx::Window> tgfx_window_;
  std::shared_ptr<tgfx::Device> tgfx_device_;
};
