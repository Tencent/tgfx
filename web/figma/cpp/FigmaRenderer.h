#include <tgfx/layers/DisplayList.h>
#include <tgfx/layers/Layer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <deque>
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
  double frameTimeCons() const;

 private:
  void dispatchMessage(const JsMessage& message);
  void render();
  tgfx::Layer* getDrawingLayer();
  void logInfo(const std::string& message) const;
  void logError(const std::string& message) const;

  // 单帧平均耗时覆盖的最大帧数
  static constexpr size_t kMaxHandMessageDurations = 30;

  std::string canvas_id_;
  std::shared_ptr<tgfx::Typeface> demo_text_typeface_;
  std::shared_ptr<tgfx::Window> tgfx_window_ = nullptr;
  std::shared_ptr<tgfx::Device> tgfx_device_ = nullptr;
  std::shared_ptr<tgfx::DisplayList> tgfx_display_list_ = nullptr;
  std::shared_ptr<tgfx::Layer> layer = nullptr;

  bool enable_info_logging_ = false;
  bool enable_error_logging_ = true;
  std::deque<double> hand_message_durations_;

};
