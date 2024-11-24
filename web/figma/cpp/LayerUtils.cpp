#include "LayerUtils.h"
#include <tgfx/core/Typeface.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/TextLayer.h>
#include <tgfx/layers/filters/BlurFilter.h>
#include <chrono>  // 添加头文件用于计时
#include <iostream>
#include "ProjectPath.h"
#include "utils.h"

// 静态变量用于统计耗时和执行次数
static double totalUpdateRectTime = 0.0;
static size_t countUpdateRect = 0;

static double totalUpdateCircleTime = 0.0;
static size_t countUpdateCircle = 0;

static double totalUpdateTextTime = 0.0;
static size_t countUpdateText = 0;

// 添加开关变量
static bool enableTimeStats = false;

// 移除构造函数的实现

template <typename T>
std::shared_ptr<T> LayerUtils::GetOrCreateLayer(tgfx::Layer* layer, const JsElement& element) {
  std::shared_ptr<T> targetLayer;
  // 先从ID里面找一下
  if (const auto existingLayer = layer->getChildByName(element.id)) {
    targetLayer = std::static_pointer_cast<T>(existingLayer);
  }
  if (!targetLayer) {
    targetLayer = T::Make();
    layer->addChild(targetLayer);
  }
  targetLayer->setName(element.id);
  auto filter = tgfx::BlurFilter::Make(16, 16);
  targetLayer->setFilters({filter});
  return targetLayer;
}

// 显式实例化模板函数
template std::shared_ptr<tgfx::ShapeLayer> LayerUtils::GetOrCreateLayer<tgfx::ShapeLayer>(
    tgfx::Layer*, const JsElement&);
template std::shared_ptr<tgfx::TextLayer> LayerUtils::GetOrCreateLayer<tgfx::TextLayer>(
    tgfx::Layer*, const JsElement&);

void LayerUtils::UpdateCanvasMatrix(tgfx::Layer* layer, const JsRect&, const JsRect& viewBox) {
  tgfx::Matrix canvasMatrix = tgfx::Matrix::MakeTrans(-viewBox.x, -viewBox.y);
  layer->setMatrix(canvasMatrix);
}

void LayerUtils::MoveShape(tgfx::Layer* layer, const std::vector<JsElement>& elements) {
  for (const auto& element : elements) {
    if (std::string errorMsg; !MoveShape(layer, element, errorMsg)) {
      std::cerr << "移动图形失败：" << errorMsg << std::endl;
    }
  }
}

bool LayerUtils::MoveShape(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg) {
  if (element.offsetX == 0 && element.offsetY == 0) {
    errorMsg = "移动距离为0";
    return false;
  }

  const auto childLayer = layer->getChildByName(element.id);
  if (!childLayer) {
    errorMsg = "未找到ID为" + element.id + "的图形";
    return false;
  }

  auto matrix = childLayer->matrix();
  matrix.postTranslate(element.offsetX, element.offsetY);
  childLayer->setMatrix(matrix);

  return true;
}

void LayerUtils::UpdateShape(tgfx::Layer* layer, const std::vector<JsElement>& elements) {
  // 重置统计变量
  totalUpdateRectTime = 0.0;
  countUpdateRect = 0;
  totalUpdateCircleTime = 0.0;
  countUpdateCircle = 0;
  totalUpdateTextTime = 0.0;
  countUpdateText = 0;

  for (const auto& element : elements) {
    if (std::string errorMsg; !UpdateShape(layer, element, errorMsg)) {
      std::cerr << "更新图形失败：" << errorMsg << std::endl;
    }
  }

  // 计算平均耗时
  double averageRectTime = countUpdateRect > 0 ? totalUpdateRectTime / countUpdateRect : 0.0f;
  double averageCircleTime =
      countUpdateCircle > 0 ? totalUpdateCircleTime / countUpdateCircle : 0.0;
  double averageTextTime = countUpdateText > 0 ? totalUpdateTextTime / countUpdateText : 0.0;

  // 计算总耗时
  double totalTime = totalUpdateRectTime + totalUpdateCircleTime + totalUpdateTextTime;

  // 计算耗时占比
  double rectTimeRatio = totalTime > 0 ? (totalUpdateRectTime / totalTime) * 100 : 0.0;
  double circleTimeRatio = totalTime > 0 ? (totalUpdateCircleTime / totalTime) * 100 : 0.0;
  double textTimeRatio = totalTime > 0 ? (totalUpdateTextTime / totalTime) * 100 : 0.0;

  // 打印到一行
  if (enableTimeStats) {
    std::cout << "UpdateRect: 平均耗时 = " << averageRectTime
              << " 微秒, 执行次数 = " << countUpdateRect << ", 耗时占比 = " << rectTimeRatio
              << "%; "
              << "UpdateCircle: 平均耗时 = " << averageCircleTime
              << " 微秒, 执行次数 = " << countUpdateCircle << ", 耗时占比 = " << circleTimeRatio
              << "%; "
              << "UpdateText: 平均耗时 = " << averageTextTime
              << " 微秒, 执行次数 = " << countUpdateText << ", 耗时占比 = " << textTimeRatio
              << "%; " << std::endl;
  }
}

bool LayerUtils::UpdateShape(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg) {
  if (element.tagName == "rect") {
    return UpdateRect(layer, element, errorMsg);
  }
  if (element.tagName == "circle") {
    return UpdateCircle(layer, element, errorMsg);
  }
  if (element.tagName == "text") {
    return UpdateText(layer, element, errorMsg);
  }
  errorMsg = "不支持的图形：" + element.tagName;
  return false;
}

bool LayerUtils::UpdateRect(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg) {
  auto start = std::chrono::high_resolution_clock::now();  // 开始计时

  auto [x, y, width, height] = element.rect;
  if (width <= 0 || height <= 0) {
    errorMsg = "矩形宽度和高度必须大于0";
    return false;
  }
  auto shapeLayer = GetOrCreateLayer<tgfx::ShapeLayer>(layer, element);
  tgfx::Path path;
  path.addRect({x, y, x + width, y + height});
  shapeLayer->setPath(path);

  auto fillStyle = tgfx::SolidColor::Make(MakeColorFromHexString(element.fill));
  shapeLayer->setFillStyle(fillStyle);

  auto end = std::chrono::high_resolution_clock::now();  // 结束计时
  std::chrono::duration<double, std::micro> duration = end - start;
  totalUpdateRectTime += duration.count();
  countUpdateRect++;

  return true;
}

bool LayerUtils::UpdateCircle(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg) {
  auto start = std::chrono::high_resolution_clock::now();  // 开始计时

  auto [cx, cy, r] = element.circle;
  if (r <= 0) {
    errorMsg = "圆形半径必须大于0";
    return false;
  }
  auto shapeLayer = GetOrCreateLayer<tgfx::ShapeLayer>(layer, element);
  tgfx::Path path;
  // 通过圆心和半径计算外接矩形
  const tgfx::Rect oval = {cx - r, cy - r, cx + r, cy + r};
  // 添加圆形路径，从0度开始，顺时针方向
  path.addOval(oval, false, 0);
  shapeLayer->setPath(path);

  auto fillStyle = tgfx::SolidColor::Make(MakeColorFromHexString(element.fill));
  shapeLayer->setFillStyle(fillStyle);

  auto end = std::chrono::high_resolution_clock::now();  // 结束计时
  std::chrono::duration<double, std::micro> duration = end - start;
  totalUpdateCircleTime += duration.count();
  countUpdateCircle++;

  return true;
}
std::shared_ptr<tgfx::Typeface> LayerUtils::currentTypeface = nullptr;
void LayerUtils::SetTypeface(const std::shared_ptr<tgfx::Typeface>& typeface) {
  currentTypeface = typeface;
}
bool LayerUtils::UpdateText(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg) {
  auto start = std::chrono::high_resolution_clock::now();  // 开始计时

  auto [x, y, fontSize, textContent] = element.text;
  if (textContent.empty()) {
    errorMsg = "文本内容不能为空";
    return false;
  }
  auto textLayer = GetOrCreateLayer<tgfx::TextLayer>(layer, element);
  textLayer->setMatrix(tgfx::Matrix::MakeTrans(x, y));
  textLayer->setText(textContent);
  textLayer->setTextColor(MakeColorFromHexString(element.fill));
  const tgfx::Font font(currentTypeface, fontSize);
  textLayer->setFont(font);

  auto end = std::chrono::high_resolution_clock::now(); // 结束计时
    std::chrono::duration<double, std::micro> duration = end - start;
    totalUpdateTextTime += duration.count();
    countUpdateText++;

    return true;
}

size_t LayerUtils::CountLayers(const std::shared_ptr<tgfx::Layer> &layer) {
    {
        size_t count = 1; // Count the current layer
        for (const auto &child: layer->children()) {
            count += CountLayers(child);
        }
        return count;
    }
}
