/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "tgfx/core/Clock.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Device.h"
#if defined(TGFX_BENCHMARK_USE_METAL)
#include "tgfx/gpu/metal/MetalDevice.h"
#elif defined(TGFX_BENCHMARK_USE_OPENGL)
#include "tgfx/gpu/opengl/GLDevice.h"
#else
#error "GlassStyleBenchmark requires a supported GPU backend."
#endif
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"

namespace {

enum class Scene {
  Plain,
  RoundedRect,
  Ellipse,
  Star,
  Grid,
};

enum class Motion {
  Static,
  Glass,
  Background,
  Parameters,
};

enum class Mode {
  Direct,
  Tiled,
};

struct BenchmarkConfig {
  Scene scene = Scene::RoundedRect;
  Motion motion = Motion::Static;
  Mode mode = Mode::Direct;
  int width = 1920;
  int height = 1080;
  int warmupFrames = 300;
  int sampleFrames = 900;
  int sampleDurationMs = 0;
  float refraction = 50.0f;
  float depth = 35.0f;
  float frost = 25.0f;
  float dispersion = 0.0f;
  float splay = 50.0f;
  float lightAngle = 135.0f;
  float lightIntensity = 50.0f;
};

struct FrameSample {
  int64_t recordingUs = 0;
  int64_t endToEndUs = 0;
};

struct Statistics {
  size_t sampleCount = 0;
  int64_t sampleDurationUs = 0;
  double recordingMeanUs = 0.0;
  double endToEndMeanUs = 0.0;
  int64_t recordingP50Us = 0;
  int64_t recordingP95Us = 0;
  int64_t recordingP99Us = 0;
  int64_t endToEndP50Us = 0;
  int64_t endToEndP95Us = 0;
  int64_t endToEndP99Us = 0;
  int over60HzBudget = 0;
  int over120HzBudget = 0;
};

enum class ParseResult {
  Success,
  Help,
  Error,
};

const char* SceneName(Scene scene) {
  switch (scene) {
    case Scene::Plain:
      return "plain";
    case Scene::RoundedRect:
      return "rounded-rect";
    case Scene::Ellipse:
      return "ellipse";
    case Scene::Star:
      return "star";
    case Scene::Grid:
      return "grid";
  }
  return "unknown";
}

const char* MotionName(Motion motion) {
  switch (motion) {
    case Motion::Static:
      return "static";
    case Motion::Glass:
      return "glass";
    case Motion::Background:
      return "background";
    case Motion::Parameters:
      return "parameters";
  }
  return "unknown";
}

const char* ModeName(Mode mode) {
  switch (mode) {
    case Mode::Direct:
      return "direct";
    case Mode::Tiled:
      return "tiled";
  }
  return "unknown";
}

const char* BackendName() {
#if defined(TGFX_BENCHMARK_USE_METAL)
  return "metal";
#else
  return "opengl";
#endif
}

void PrintUsage() {
  std::cout << "Usage: GlassStyleBenchmark [options]\n"
            << "  --scene plain|rounded-rect|ellipse|star|grid\n"
            << "  --motion static|glass|background|parameters\n"
            << "  --mode direct|tiled\n"
            << "  --width <128-4096> --height <128-4096>\n"
            << "  --warmup <0-100000>\n"
            << "  --frames <1-100000> or --duration-ms <1-600000>\n"
            << "  --refraction <0-100> --depth <0-100> --frost <0-100>\n"
            << "  --dispersion <0-100> --splay <0-100>\n"
            << "  --light-angle <0-360> --light-intensity <0-100>\n";
}

bool ParseInteger(const char* text, int* value) {
  errno = 0;
  char* endPointer = nullptr;
  auto parsed = std::strtol(text, &endPointer, 10);
  if (errno != 0 || endPointer == text || *endPointer != '\0' || parsed < INT_MIN ||
      parsed > INT_MAX) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

bool ParseFloat(const char* text, float* value) {
  errno = 0;
  char* endPointer = nullptr;
  auto parsed = std::strtof(text, &endPointer);
  if (errno != 0 || endPointer == text || *endPointer != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  *value = parsed;
  return true;
}

bool ParseScene(const char* text, Scene* scene) {
  std::string value = text;
  if (value == "plain") {
    *scene = Scene::Plain;
  } else if (value == "rounded-rect") {
    *scene = Scene::RoundedRect;
  } else if (value == "ellipse") {
    *scene = Scene::Ellipse;
  } else if (value == "star") {
    *scene = Scene::Star;
  } else if (value == "grid") {
    *scene = Scene::Grid;
  } else {
    return false;
  }
  return true;
}

bool ParseMotion(const char* text, Motion* motion) {
  std::string value = text;
  if (value == "static") {
    *motion = Motion::Static;
  } else if (value == "glass") {
    *motion = Motion::Glass;
  } else if (value == "background") {
    *motion = Motion::Background;
  } else if (value == "parameters") {
    *motion = Motion::Parameters;
  } else {
    return false;
  }
  return true;
}

bool ParseMode(const char* text, Mode* mode) {
  std::string value = text;
  if (value == "direct") {
    *mode = Mode::Direct;
  } else if (value == "tiled") {
    *mode = Mode::Tiled;
  } else {
    return false;
  }
  return true;
}

bool IsGlassParameter(float value) {
  return value >= 0.0f && value <= 100.0f;
}

ParseResult ParseCommandLine(int argc, char* argv[], BenchmarkConfig* config) {
  bool hasFrameCount = false;
  bool hasDuration = false;
  for (int index = 1; index < argc; ++index) {
    std::string option = argv[index];
    if (option == "--help") {
      return ParseResult::Help;
    }
    if (index + 1 >= argc) {
      std::cerr << "Missing value for " << option << ".\n";
      return ParseResult::Error;
    }
    const char* value = argv[++index];
    bool parsed = false;
    if (option == "--scene") {
      parsed = ParseScene(value, &config->scene);
    } else if (option == "--motion") {
      parsed = ParseMotion(value, &config->motion);
    } else if (option == "--mode") {
      parsed = ParseMode(value, &config->mode);
    } else if (option == "--width") {
      parsed = ParseInteger(value, &config->width);
    } else if (option == "--height") {
      parsed = ParseInteger(value, &config->height);
    } else if (option == "--warmup") {
      parsed = ParseInteger(value, &config->warmupFrames);
    } else if (option == "--frames") {
      parsed = ParseInteger(value, &config->sampleFrames);
      hasFrameCount = true;
    } else if (option == "--duration-ms") {
      parsed = ParseInteger(value, &config->sampleDurationMs);
      hasDuration = true;
    } else if (option == "--refraction") {
      parsed = ParseFloat(value, &config->refraction);
    } else if (option == "--depth") {
      parsed = ParseFloat(value, &config->depth);
    } else if (option == "--frost") {
      parsed = ParseFloat(value, &config->frost);
    } else if (option == "--dispersion") {
      parsed = ParseFloat(value, &config->dispersion);
    } else if (option == "--splay") {
      parsed = ParseFloat(value, &config->splay);
    } else if (option == "--light-angle") {
      parsed = ParseFloat(value, &config->lightAngle);
    } else if (option == "--light-intensity") {
      parsed = ParseFloat(value, &config->lightIntensity);
    } else {
      std::cerr << "Unknown option " << option << ".\n";
      return ParseResult::Error;
    }
    if (!parsed) {
      std::cerr << "Invalid value for " << option << ": " << value << ".\n";
      return ParseResult::Error;
    }
  }
  if (hasFrameCount && hasDuration) {
    std::cerr << "--frames and --duration-ms cannot be used together.\n";
    return ParseResult::Error;
  }
  if (config->width < 128 || config->width > 4096 || config->height < 128 ||
      config->height > 4096 || config->warmupFrames < 0 || config->warmupFrames > 100000 ||
      config->sampleFrames < 1 || config->sampleFrames > 100000 ||
      (hasDuration && (config->sampleDurationMs < 1 || config->sampleDurationMs > 600000)) ||
      !IsGlassParameter(config->refraction) || !IsGlassParameter(config->depth) ||
      !IsGlassParameter(config->frost) || !IsGlassParameter(config->dispersion) ||
      !IsGlassParameter(config->splay) || config->lightAngle < 0.0f ||
      config->lightAngle > 360.0f || !IsGlassParameter(config->lightIntensity)) {
    std::cerr << "The benchmark configuration is outside its supported range.\n";
    return ParseResult::Error;
  }
  return ParseResult::Success;
}

std::shared_ptr<tgfx::SolidLayer> MakeSolidLayer(float width, float height, const tgfx::Color& color,
                                                 float x, float y) {
  auto layer = tgfx::SolidLayer::Make();
  layer->setWidth(width);
  layer->setHeight(height);
  layer->setColor(color);
  layer->setMatrix(tgfx::Matrix::MakeTrans(x, y));
  return layer;
}

std::shared_ptr<tgfx::ShapeLayer> MakeShapeLayer(const tgfx::Path& path, const tgfx::Color& color,
                                                  float x, float y) {
  auto layer = tgfx::ShapeLayer::Make();
  layer->setPath(path);
  layer->setFillStyle(tgfx::ShapeStyle::Make(color));
  layer->setMatrix(tgfx::Matrix::MakeTrans(x, y));
  return layer;
}

class BenchmarkScene {
 public:
  BenchmarkScene(const BenchmarkConfig& config, bool glassEnabled)
      : config(config), glassEnabled(glassEnabled) {
    displayList.setRenderMode(config.mode == Mode::Direct ? tgfx::RenderMode::Direct
                                                           : tgfx::RenderMode::Tiled);
    if (config.mode == Mode::Tiled) {
      displayList.setTileUpdateMode(tgfx::TileUpdateMode::Smooth);
      displayList.setMaxTileCount(512);
    }
    buildBackground();
    buildGlassPanels();
  }

  bool renderFrame(tgfx::Context* context, tgfx::Surface* surface, int frameIndex,
                   FrameSample* sample) {
    updateFrame(frameIndex);
    auto startTime = tgfx::Clock::Now();
    surface->getCanvas()->clear();
    displayList.render(surface, false);
    auto recording = context->flush();
    auto recordingTime = tgfx::Clock::Now();
    if (recording == nullptr) {
      return false;
    }
    // Synchronize every sample so end-to-end time includes completed GPU work.
    context->submit(std::move(recording), true);
    auto completedTime = tgfx::Clock::Now();
    if (sample != nullptr) {
      sample->recordingUs = recordingTime - startTime;
      sample->endToEndUs = completedTime - startTime;
    }
    return true;
  }

 private:
  void buildBackground() {
    backgroundLayer = tgfx::Layer::Make();
    backgroundLayer->addChild(MakeSolidLayer(static_cast<float>(config.width),
                                             static_cast<float>(config.height),
                                             tgfx::Color::FromRGBA(15, 29, 66), 0.0f, 0.0f));

    auto bluePanel = MakeSolidLayer(static_cast<float>(config.width) * 0.42f,
                                    static_cast<float>(config.height) * 0.50f,
                                    tgfx::Color::FromRGBA(20, 120, 255),
                                    static_cast<float>(config.width) * 0.08f,
                                    static_cast<float>(config.height) * 0.12f);
    bluePanel->setRadiusX(64.0f);
    bluePanel->setRadiusY(64.0f);
    backgroundLayer->addChild(bluePanel);

    tgfx::Path greenCircle = {};
    greenCircle.addOval(tgfx::Rect::MakeXYWH(static_cast<float>(config.width) * 0.55f,
                                              static_cast<float>(config.height) * 0.18f,
                                              static_cast<float>(config.width) * 0.30f,
                                              static_cast<float>(config.width) * 0.30f));
    backgroundLayer->addChild(
        MakeShapeLayer(greenCircle, tgfx::Color::FromRGBA(50, 220, 140, 220), 0.0f, 0.0f));

    auto pinkPanel = MakeSolidLayer(static_cast<float>(config.width) * 0.48f,
                                    static_cast<float>(config.height) * 0.28f,
                                    tgfx::Color::FromRGBA(224, 71, 159),
                                    static_cast<float>(config.width) * 0.36f,
                                    static_cast<float>(config.height) * 0.62f);
    pinkPanel->setRadiusX(48.0f);
    pinkPanel->setRadiusY(48.0f);
    backgroundLayer->addChild(pinkPanel);

    tgfx::Path yellowCircle = {};
    yellowCircle.addOval(tgfx::Rect::MakeXYWH(static_cast<float>(config.width) * 0.08f,
                                               static_cast<float>(config.height) * 0.65f,
                                               static_cast<float>(config.width) * 0.20f,
                                               static_cast<float>(config.width) * 0.20f));
    backgroundLayer->addChild(
        MakeShapeLayer(yellowCircle, tgfx::Color::FromRGBA(255, 205, 68, 230), 0.0f, 0.0f));
    displayList.root()->addChild(backgroundLayer);
  }

  void buildGlassPanels() {
    switch (config.scene) {
      case Scene::Plain:
      case Scene::RoundedRect:
        addSolidPanel(480.0f, 320.0f, 48.0f, 48.0f, false, 0);
        break;
      case Scene::Ellipse:
        addSolidPanel(480.0f, 320.0f, 240.0f, 160.0f, true, 0);
        break;
      case Scene::Star:
        addStarPanel(360.0f, 0);
        break;
      case Scene::Grid:
        addSolidPanel(360.0f, 240.0f, 36.0f, 36.0f, false, 0);
        addSolidPanel(360.0f, 240.0f, 36.0f, 36.0f, false, 1);
        addSolidPanel(360.0f, 240.0f, 36.0f, 36.0f, false, 2);
        addSolidPanel(360.0f, 240.0f, 36.0f, 36.0f, false, 3);
        break;
    }
  }

  void addSolidPanel(float requestedWidth, float requestedHeight, float radiusX, float radiusY,
                     bool ellipse, int gridIndex) {
    auto availableWidth = static_cast<float>(config.width) - 64.0f;
    auto availableHeight = static_cast<float>(config.height) - 64.0f;
    auto width = std::max(32.0f, std::min(requestedWidth, availableWidth));
    auto height = std::max(32.0f, std::min(requestedHeight, availableHeight));
    auto position = panelPosition(width, height, gridIndex);
    auto layer = MakeSolidLayer(width, height, tgfx::Color::FromRGBA(255, 255, 255, 46),
                                position.x, position.y);
    layer->setRadiusX(ellipse ? width * 0.5f : std::min(radiusX, width * 0.5f));
    layer->setRadiusY(ellipse ? height * 0.5f : std::min(radiusY, height * 0.5f));
    if (glassEnabled) {
      auto style = makeGlassStyle();
      style->setCornerRadius(ellipse ? 0.0f : std::min(radiusX, radiusY));
      layer->setLayerStyles({style});
      glassStyles.push_back(style);
    }
    glassLayers.push_back(layer);
    glassPositions.push_back(position);
    displayList.root()->addChild(layer);
  }

  void addStarPanel(float requestedSize, int gridIndex) {
    auto size = std::max(32.0f, std::min(requestedSize,
                                         static_cast<float>(std::min(config.width, config.height)) -
                                             64.0f));
    auto position = panelPosition(size, size, gridIndex);
    auto layer = tgfx::ShapeLayer::Make();
    layer->setPath(makeStarPath(size));
    layer->setFillStyle(tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(255, 255, 255, 46)));
    layer->setMatrix(tgfx::Matrix::MakeTrans(position.x, position.y));
    if (glassEnabled) {
      auto style = makeGlassStyle();
      layer->setLayerStyles({style});
      glassStyles.push_back(style);
    }
    glassLayers.push_back(layer);
    glassPositions.push_back(position);
    displayList.root()->addChild(layer);
  }

  tgfx::Point panelPosition(float width, float height, int gridIndex) const {
    if (config.scene != Scene::Grid) {
      return tgfx::Point::Make((static_cast<float>(config.width) - width) * 0.5f,
                               (static_cast<float>(config.height) - height) * 0.5f);
    }
    auto column = gridIndex % 2;
    auto row = gridIndex / 2;
    auto horizontalGap = 32.0f;
    auto verticalGap = 32.0f;
    auto totalWidth = width * 2.0f + horizontalGap;
    auto totalHeight = height * 2.0f + verticalGap;
    return tgfx::Point::Make((static_cast<float>(config.width) - totalWidth) * 0.5f +
                                 static_cast<float>(column) * (width + horizontalGap),
                             (static_cast<float>(config.height) - totalHeight) * 0.5f +
                                 static_cast<float>(row) * (height + verticalGap));
  }

  tgfx::Path makeStarPath(float size) const {
    constexpr float Pi = 3.14159265358979323846f;
    auto halfSize = size * 0.5f;
    auto outerRadius = halfSize * 0.9f;
    auto innerRadius = outerRadius * 0.382f;
    auto startAngle = -Pi * 0.5f;
    tgfx::Path path = {};
    for (int index = 0; index < 5; ++index) {
      auto outerAngle = startAngle + static_cast<float>(index) * Pi * 0.8f;
      auto innerAngle = outerAngle + Pi * 0.2f;
      auto outerX = halfSize + outerRadius * std::cos(outerAngle);
      auto outerY = halfSize + outerRadius * std::sin(outerAngle);
      auto innerX = halfSize + innerRadius * std::cos(innerAngle);
      auto innerY = halfSize + innerRadius * std::sin(innerAngle);
      if (index == 0) {
        path.moveTo(outerX, outerY);
      } else {
        path.lineTo(outerX, outerY);
      }
      path.lineTo(innerX, innerY);
    }
    path.close();
    return path;
  }

  std::shared_ptr<tgfx::GlassStyle> makeGlassStyle() const {
    return tgfx::GlassStyle::Make(config.refraction, config.depth, config.frost, config.dispersion,
                                  config.splay, config.lightAngle, config.lightIntensity);
  }

  void updateFrame(int frameIndex) {
    auto offset = animationOffset(frameIndex);
    if (config.motion == Motion::Glass) {
      for (size_t index = 0; index < glassLayers.size(); ++index) {
        const auto& position = glassPositions[index];
        glassLayers[index]->setMatrix(tgfx::Matrix::MakeTrans(position.x + offset, position.y));
      }
    } else if (config.motion == Motion::Background) {
      backgroundLayer->setMatrix(tgfx::Matrix::MakeTrans(-offset, 0.0f));
    } else if (config.motion == Motion::Parameters && glassEnabled) {
      auto alternate = config.refraction <= 50.0f ? std::min(100.0f, config.refraction + 20.0f)
                                                   : std::max(0.0f, config.refraction - 20.0f);
      auto value = ((frameIndex / 30) % 2 == 0) ? config.refraction : alternate;
      for (const auto& style : glassStyles) {
        style->setRefraction(value);
      }
    }
  }

  static float animationOffset(int frameIndex) {
    auto phase = frameIndex % 240;
    return static_cast<float>(phase < 120 ? phase - 60 : 180 - phase);
  }

  const BenchmarkConfig& config;
  bool glassEnabled = false;
  tgfx::DisplayList displayList = {};
  std::shared_ptr<tgfx::Layer> backgroundLayer = nullptr;
  std::vector<std::shared_ptr<tgfx::Layer>> glassLayers = {};
  std::vector<std::shared_ptr<tgfx::GlassStyle>> glassStyles = {};
  std::vector<tgfx::Point> glassPositions = {};
};

bool RunScene(tgfx::Context* context, tgfx::Surface* surface, BenchmarkScene* scene,
              const BenchmarkConfig& config, std::vector<FrameSample>* samples,
              int64_t* sampleDurationUs) {
  FrameSample ignored = {};
  for (int frame = 0; frame < config.warmupFrames; ++frame) {
    if (!scene->renderFrame(context, surface, frame, &ignored)) {
      return false;
    }
  }
  samples->clear();
  if (config.sampleDurationMs == 0) {
    samples->reserve(static_cast<size_t>(config.sampleFrames));
  }
  auto sampleStartTime = tgfx::Clock::Now();
  auto frameIndex = config.warmupFrames;
  if (config.sampleDurationMs > 0) {
    auto requestedDurationUs = static_cast<int64_t>(config.sampleDurationMs) * 1000;
    do {
      FrameSample sample = {};
      if (!scene->renderFrame(context, surface, frameIndex, &sample)) {
        return false;
      }
      samples->push_back(sample);
      ++frameIndex;
    } while (tgfx::Clock::Now() - sampleStartTime < requestedDurationUs);
  } else {
    for (int frame = 0; frame < config.sampleFrames; ++frame) {
      FrameSample sample = {};
      if (!scene->renderFrame(context, surface, frameIndex, &sample)) {
        return false;
      }
      samples->push_back(sample);
      ++frameIndex;
    }
  }
  *sampleDurationUs = tgfx::Clock::Now() - sampleStartTime;
  return true;
}

int64_t Percentile(std::vector<int64_t> values, double percentile) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  auto rank = static_cast<size_t>(std::ceil(percentile * static_cast<double>(values.size())));
  auto index = std::min(values.size() - 1, std::max<size_t>(1, rank) - 1);
  return values[index];
}

Statistics CollectStatistics(const std::vector<FrameSample>& samples, int64_t sampleDurationUs) {
  Statistics statistics = {};
  statistics.sampleCount = samples.size();
  statistics.sampleDurationUs = sampleDurationUs;
  std::vector<int64_t> recording = {};
  std::vector<int64_t> endToEnd = {};
  recording.reserve(samples.size());
  endToEnd.reserve(samples.size());
  int64_t recordingSum = 0;
  int64_t endToEndSum = 0;
  for (const auto& sample : samples) {
    recording.push_back(sample.recordingUs);
    endToEnd.push_back(sample.endToEndUs);
    recordingSum += sample.recordingUs;
    endToEndSum += sample.endToEndUs;
    if (sample.endToEndUs > 16667) {
      ++statistics.over60HzBudget;
    }
    if (sample.endToEndUs > 8333) {
      ++statistics.over120HzBudget;
    }
  }
  if (!samples.empty()) {
    auto count = static_cast<double>(samples.size());
    statistics.recordingMeanUs = static_cast<double>(recordingSum) / count;
    statistics.endToEndMeanUs = static_cast<double>(endToEndSum) / count;
  }
  statistics.recordingP50Us = Percentile(recording, 0.50);
  statistics.recordingP95Us = Percentile(recording, 0.95);
  statistics.recordingP99Us = Percentile(recording, 0.99);
  statistics.endToEndP50Us = Percentile(endToEnd, 0.50);
  statistics.endToEndP95Us = Percentile(endToEnd, 0.95);
  statistics.endToEndP99Us = Percentile(endToEnd, 0.99);
  return statistics;
}

void PrintCsvHeader() {
  std::cout << "role,backend,scene,motion,mode,width,height,warmup_frames,sample_frames,"
            << "sample_duration_ms,refraction,depth,frost,dispersion,splay,light_angle,"
            << "light_intensity,recording_mean_us,recording_p50_us,recording_p95_us,"
            << "recording_p99_us,end_to_end_mean_us,end_to_end_p50_us,end_to_end_p95_us,"
            << "end_to_end_p99_us,over_60hz_budget,over_120hz_budget,"
            << "baseline_end_to_end_mean_us,delta_end_to_end_mean_us,"
            << "delta_end_to_end_p95_us\n";
}

void PrintCsvRow(const char* role, const BenchmarkConfig& config, const Statistics& statistics,
                 const Statistics& baseline) {
  std::cout << std::fixed << std::setprecision(3) << role << ',' << BackendName() << ','
            << SceneName(config.scene) << ',' << MotionName(config.motion) << ','
            << ModeName(config.mode) << ',' << config.width << ',' << config.height << ','
            << config.warmupFrames << ',' << statistics.sampleCount << ','
            << static_cast<double>(statistics.sampleDurationUs) / 1000.0 << ',' << config.refraction
            << ',' << config.depth << ',' << config.frost << ',' << config.dispersion << ','
            << config.splay << ',' << config.lightAngle << ',' << config.lightIntensity << ','
            << statistics.recordingMeanUs << ',' << statistics.recordingP50Us << ','
            << statistics.recordingP95Us << ',' << statistics.recordingP99Us << ','
            << statistics.endToEndMeanUs << ',' << statistics.endToEndP50Us << ','
            << statistics.endToEndP95Us << ',' << statistics.endToEndP99Us << ','
            << statistics.over60HzBudget << ',' << statistics.over120HzBudget << ','
            << baseline.endToEndMeanUs << ','
            << statistics.endToEndMeanUs - baseline.endToEndMeanUs << ','
            << statistics.endToEndP95Us - baseline.endToEndP95Us << '\n';
}

std::shared_ptr<tgfx::Device> MakeDevice() {
#if defined(TGFX_BENCHMARK_USE_METAL)
  return tgfx::MetalDevice::Make();
#else
  return tgfx::GLDevice::Make();
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  BenchmarkConfig config = {};
  auto parseResult = ParseCommandLine(argc, argv, &config);
  if (parseResult == ParseResult::Help) {
    PrintUsage();
    return 0;
  }
  if (parseResult == ParseResult::Error) {
    PrintUsage();
    return 1;
  }

  auto device = MakeDevice();
  if (device == nullptr) {
    std::cerr << "Failed to create the GPU device.\n";
    return 1;
  }
  auto context = device->lockContext();
  if (context == nullptr) {
    std::cerr << "Failed to lock the GPU context.\n";
    return 1;
  }
  auto surface = tgfx::Surface::Make(context, config.width, config.height);
  if (surface == nullptr) {
    std::cerr << "Failed to create the benchmark surface.\n";
    device->unlock();
    return 1;
  }

  std::vector<FrameSample> baselineSamples = {};
  int64_t baselineDurationUs = 0;
  BenchmarkScene baselineScene(config, false);
  if (!RunScene(context, surface.get(), &baselineScene, config, &baselineSamples,
                &baselineDurationUs)) {
    std::cerr << "The plain baseline produced no GPU recording.\n";
    device->unlock();
    return 1;
  }
  auto baseline = CollectStatistics(baselineSamples, baselineDurationUs);

  PrintCsvHeader();
  PrintCsvRow("baseline", config, baseline, baseline);
  if (config.scene != Scene::Plain) {
    std::vector<FrameSample> glassSamples = {};
    int64_t glassDurationUs = 0;
    BenchmarkScene glassScene(config, true);
    if (!RunScene(context, surface.get(), &glassScene, config, &glassSamples, &glassDurationUs)) {
      std::cerr << "The GlassStyle scene produced no GPU recording.\n";
      device->unlock();
      return 1;
    }
    auto glass = CollectStatistics(glassSamples, glassDurationUs);
    PrintCsvRow("glass", config, glass, baseline);
  }

  device->unlock();
  return 0;
}
