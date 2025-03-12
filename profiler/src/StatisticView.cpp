/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "StatisticView.h"
#include <QSGImageNode>
#include <QTimer>
#include "../../src/core/shaders/GradientShader.h"
#include "MainView.h"
#include "SourceView.h"

StatisticsView::StatisticsView(tracy::Worker& _worker, ViewData& _viewData, View* _view, FramesView* _framesView, SourceView* _srcView, QQuickItem* parent) :
  QQuickItem(parent),
  worker(_worker),
  viewData(_viewData),
  view(_view),
  framesView(_framesView),
  srcView(_srcView)
  {
  model = new StatisticsModel(worker, viewData, view, this);
  model->setStatisticsMode(StatisticsModel::Instrumentation);
  delegate = new StatisticsDelegate(model, view, this);
  setFlag(ItemHasContents, true);
  updateZoneCountLabels();

  qmlRegisterType<FpsChartItem>("TGFX.Profiler", 1, 0, "FpsChart");
  qmlRegisterType<FpsChartRect>("TGFX.Profiler", 1, 0, "FpsBarChart");
  fpsUpdateTimer = new QTimer(this);
  connect(fpsUpdateTimer, &QTimer::timeout, this, &StatisticsView::refreshFpsData);
  fpsUpdateTimer->start(200);

  dataRefreshTimer = new QTimer(this);
  connect(dataRefreshTimer, &QTimer::timeout, this, &StatisticsView::refreshTableData);
  dataRefreshTimer->start(500);

}

StatisticsView::~StatisticsView() {
  if(fpsUpdateTimer) {
    fpsUpdateTimer->stop();
  }
  if(dataRefreshTimer) {
    dataRefreshTimer->stop();
  }
};


QString StatisticsView::getTotalZoneCount() const {
  if(!model) return "0";
  return QString::number(model->getTotalZoneCount());
}

QString StatisticsView::getVisibleZoneCount() const {
  if(!model) return "0";
  return QString::number(model->getVisibleZoneCount());
}

QVector<float> StatisticsView::getFpsValues() const {
  if (!model) {
    return {QVector<float>()};
  }
  return model->getFpsValues();
}

float StatisticsView::getMinFps() const {
  if (!model) return 0.0f;
  return model->getMinFps();
}

float StatisticsView::getMaxFps() const {
  if (!model) return 0.0f;
  return model->getMaxFps();
}

float StatisticsView::getAvgFps() const {
  if (!model) return 0.0f;
  return model->getAvgFps();
}

void StatisticsView::refreshFpsData() {
  if (model) {
    model->resetFrameDataCache();
    Q_EMIT fpsDataChanged();
  }
}
void StatisticsView::refreshTableData() {
  if(model) {
    model->refreshData();
    updateZoneCountLabels();
  }
}

bool StatisticsView::isLimitRangeActive() const {
  return view->m_statRange.active;
}

void StatisticsView::setLimitRangeActive(bool active) {
  if(active != view->m_statRange.active) {
    if(active) {
      view->m_statRange.active = true;
      view->m_statRange.min = viewData.zvStart;
      view->m_statRange.max = viewData.zvEnd;
      model->setStatRange(view->m_statRange.min, view->m_statRange.max, true);
    }
    else {
      view->m_statRange.active = false;
      model->setStatRange(0, 0, false);
    }
    model->refreshData();
    updateZoneCountLabels();
    Q_EMIT limitRangeActiveChanged();
  }
}

QString StatisticsView::getFilterText() const {
  if(!model) return "";
  return model->FilterText();
}

void StatisticsView::setFilterText(const QString& text) {
  if(!model) return;

  if(model->getFilterText() != text) {
    model->setFilterText(text);
    Q_EMIT filterTextChanged();
  }
}

void StatisticsView::openSource(int row) {
  //if(!row < 0 || row >= model->rowCount()) return;

  auto& srcloc = model->getSrcLocFromIndex(model->index(row, StatisticsModel::LocationColumn));
  const char* fileName = worker.GetString(srcloc.file);
  int line = static_cast<int>(srcloc.line);

  viewSource(fileName, line);
}

void StatisticsView::viewSource(const char* fileName, int line) {
  if(!fileName || !model || !view) return;

  srcViewFile = fileName;
  model->openSource(fileName, line, worker, view);

  if(!srcView) {
    srcView = new SourceView(nullptr);
    srcView->setAttribute(Qt::WA_DeleteOnClose);
    srcView->setStyleSheet("background-color: #2D2D2D;");
    connect(srcView, &QObject::destroyed, this, [this](){srcView = nullptr;});
  }

  const auto& source = model->getSource();
  if(!source.empty()) {
    QString content = QString::fromStdString(std::string(source.data(), source.dataSize()));
    srcView->setWindowTitle(QString("Source: %1").arg(fileName));
    srcView->loadSource(content, line);
    srcView->show();
    srcView->raise();
    srcView->activateWindow();
  }
}

void StatisticsView::setAccumulationMode(int mode) {
  model->setAccumulationMode(static_cast<StatisticsModel::AccumulationMode>(mode));
  updateZoneCountLabels();
}

void StatisticsView::updateZoneCountLabels() {
  Q_EMIT zoneCountChanged();
}

void StatisticsView::sort(int column, Qt::SortOrder order) {
  model->sort(column, order);
}

void StatisticsView::clearFilter() {
  setFilterText("");
}

bool StatisticsView::srcFileValid(const char* fn, uint64_t olderThan, const tracy::Worker& worker,
                                  View* view) {
  if (worker.GetSourceFileFromCache(fn).data != nullptr) return true;
  struct stat buf = {};
  if (stat(view->sourceSubstitution(fn), &buf) == 0 && (buf.st_mode & S_IFREG) != 0) {
    if (!view->validateSourceAge()) return true;
    return (uint64_t)buf.st_mtime < olderThan;
  }
  return false;
}

void StatisticsView::onStatRangeChanged(int64_t start, int64_t end, bool active) {
  if(isLimitRangeActive()) {
    view->m_statRange.min = start;
    view->m_statRange.max = end;
    model->setStatRange(start, end, active);
    model->refreshData();
    updateZoneCountLabels();
  }
}

/////*fps chart class*//////
FpsChartItem::FpsChartItem(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setFlag(ItemAcceptsInputMethod, true);
  setFlag(ItemIsFocusScope, true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
  setAntialiasing(true);
  createAppHost();

  animationTimer = new QTimer(this);
  connect(animationTimer, &QTimer::timeout, this, &FpsChartItem::updateAnimation);
}

FpsChartItem::~FpsChartItem() {
  if(animationTimer) {
    animationTimer->stop();
    delete animationTimer;
  }
}

void FpsChartItem::createAppHost() {
  appHost = std::make_unique<AppHost>();
#ifdef __APPLE__
  auto defaultTypeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
  auto emojiTypeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
#else
  auto defaultTypeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
#endif
  appHost->addTypeface("default", defaultTypeface);
}

void FpsChartItem::setFpsValues(const QVector<float>& values) {
  if(_fpsValues != values) {
    _previousFpsValues = _fpsValues;
    _fpsValues = values;
    _animationProgress = 0.0f;
    startAnimation();
    update();
    Q_EMIT fpsValuesChanged();
  }
}

void FpsChartItem::setMinFps(float value) {
  if(!qFuzzyCompare(_minFps, value)) {
    _preMinFps = _minFps;
    _minFps = value;
    if(_animationProgress >= 1.0f) {
      _animationProgress = 0.0f;
      startAnimation();
    }
    update();
    Q_EMIT minFpsChanged();
  }
}
void FpsChartItem::setMaxFps(float value) {
  if(!qFuzzyCompare(_maxFps, value)) {
    _preMaxFps = _maxFps;
    _maxFps = value;
    if(_animationProgress >= 1.0f) {
      _animationProgress = 0.0f;
      startAnimation();
    }
    update();
    Q_EMIT maxFpsChanged();
  }
}

void FpsChartItem::setAvgFps(float value) {
  if (!qFuzzyCompare(_avgFps, value)) {
    _preAvgFps = _avgFps;
    _avgFps = value;
    if(_animationProgress >= 1.0f) {
      _animationProgress = 0.0f;
      startAnimation();
    }
    update();
    Q_EMIT avgFpsChanged();
  }
}

void FpsChartItem::draw() {
  auto device = tgfxWindow->getDevice();
  if(device == nullptr) {
    return;
  }

  auto context = device->lockContext();
  if(context == nullptr) {
    return;
  }

  auto surface = tgfxWindow->getSurface(context);
  if(surface == nullptr) {
    device->unlock();
    return;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  //drawRect(canvas, 0, 0, static_cast<float>(width()),static_cast<float>(height()),0xFF121212 );
  drawFps(canvas);
  canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}

void FpsChartItem::drawFps(tgfx::Canvas* canvas) {
  if(_fpsValues.isEmpty()) return;

  canvas->save();
  canvas->translate(viewOffset, 0);

  float minFps = getInterpolatedValue(_preMinFps, _minFps);
  float maxFps = getInterpolatedValue(_preMaxFps, _maxFps);
  //float avgFps = getInterpolatedValue(_preAvgFps, _avgFps);

  float fpsMin = qMax(0.0f, minFps * 0.8f);
  float fpsMax = maxFps * 1.2f;
  float range = fpsMax - fpsMin;

  if(range <= 0.1f) {
    range = 60.0f;
  }

  auto chartWidth = static_cast<float>(width() - 40);
  auto chartHeight = static_cast<float>(height() - 40);
  float xStart = 20;
  float yStart = 20;

  const int gridLines = 5;
  tgfx::Paint gridPaint;
  gridPaint.setColor(getTgfxColor(0x30FFFFFF));
  gridPaint.setStrokeWidth(1.0f);
  gridPaint.setStyle(tgfx::PaintStyle::Stroke);
  for (int i = 0; i <= gridLines; ++i) {
    float y = yStart + chartHeight * i / gridLines;
    canvas->drawLine(xStart, y, xStart + chartWidth, y, gridPaint);
    float fpsValue = fpsMax - ((range * i) / gridLines);
    std::string label = std::to_string(static_cast<int>(fpsValue)) + "FPS";
    drawText(canvas, appHost.get(),label, xStart - 5, y - 5, 0xFFAAAAAA);
  }

  int dataPoints = static_cast<int>(_fpsValues.size());
  int verticalLines = qMin(10, dataPoints);
  for(int i = 0; i <= verticalLines; ++i) {
    float x = xStart + chartWidth * i / verticalLines;
    canvas->drawLine(x, yStart, x, yStart + chartHeight, gridPaint);
  }

  QVector<float> interpolatedValues;
  int count = static_cast<int>(qMin(_fpsValues.size(), _previousFpsValues.size()));

  for(int i = 0; i < count; ++i) {
    float oldVal = _previousFpsValues[i];
    float newVal = _fpsValues[i];
    float interpolated = getInterpolatedValue(oldVal, newVal);
    interpolatedValues.append(interpolated);
  }

  for(int i = count; i < _fpsValues.size(); ++i) {
    interpolatedValues.append(_fpsValues[i] * _animationProgress);
  }

  for(int i = count; i < _previousFpsValues.size(); ++i) {
    interpolatedValues.append(_previousFpsValues[i] * (1.0f - _animationProgress));
  }

  if(!interpolatedValues.isEmpty()) {
    tgfx::Path fpsPath;
    tgfx::Paint fpsPaint;
    fpsPaint.setColor(getTgfxColor(0xFFBD94AB));
    fpsPaint.setStrokeWidth(2.0f);
    fpsPaint.setStyle(tgfx::PaintStyle::Stroke);
    fpsPaint.setAntiAlias(true);

    float xStep = chartWidth / (interpolatedValues.size() - 1);
    float x = xStart;

    float firstFps = interpolatedValues[0];
    float y = yStart + chartHeight * (1.0f - (firstFps - fpsMin) / range);
    y = qBound(yStart, y, yStart + chartHeight);
    fpsPath.moveTo(x, y);

    for(int i = 1; i < interpolatedValues.size(); ++i) {
      x += xStep;
      float fps = interpolatedValues[i];
      y = yStart + chartHeight * (1.0f - (fps - fpsMin) / range);
      fpsPath.lineTo(x, y);
    }

    canvas->drawPath(fpsPath, fpsPaint);

    tgfx::Path fillPath = fpsPath;
    fillPath.lineTo(x, yStart + chartHeight);
    fillPath.lineTo(xStart, yStart + chartHeight);
    fillPath.close();

    tgfx::Paint fillPaint;
    const tgfx::Color colors[] = {
        {0.62f, 0.58f, 0.67f, 0.7f},
        {0.62f, 0.58f, 0.67f, 0.0f}
    };
    const float positions[] = {0.0f, 1.0f};
    auto gradient = tgfx::GradientShader::MakeLinearGradient(
        {xStart, yStart},
        {xStart, yStart + chartHeight},
        std::vector<tgfx::Color>(colors, colors + 2),
        std::vector<float>(positions, positions + 2)
    );
    fillPaint.setShader(gradient);
    canvas->drawPath(fillPath, fillPaint);
  }

  canvas->restore();
}

void FpsChartItem::startAnimation() {
  if(!animationTimer->isActive()) {
    animationTimer->start(16);
  }
}

void FpsChartItem::updateAnimation() {
  _animationProgress += 0.1f;
  if (_animationProgress >= 1.0f) {
    _animationProgress = 1.0f;
    animationTimer->stop();
  }
}

float FpsChartItem::getInterpolatedValue(float oldVal, float newVal) const {
  return oldVal + (_animationProgress * (newVal - oldVal));
}

QSGNode* FpsChartItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  auto node = dynamic_cast<QSGImageNode*>(oldNode);
  if (!tgfxWindow) {
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
  }

  auto pixelRadio = window()->devicePixelRatio();
  auto screenWidth = static_cast<int>(ceil(width() * pixelRadio));
  auto screenHeight = static_cast<int>(ceil((float)height() * pixelRadio));
  auto sizeChanged =
      appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRadio));
  if (sizeChanged) {
    tgfxWindow->invalidSize();
  }
  draw();
  auto texture = tgfxWindow->getQSGTexture();
  if (texture) {
    if (node == nullptr) {
      node = window()->createImageNode();
    }

    node->setTexture(texture);
    node->markDirty(QSGNode::DirtyMaterial);
    node->setRect(boundingRect());
  }
  return node;
}


//////*FpsChart paint rect style*//////
FpsChartRect::FpsChartRect(QQuickItem* parent): QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setFlag(ItemAcceptsInputMethod, true);
  setFlag(ItemIsFocusScope, true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
  setAntialiasing(true);
  createAppHost();

  animationTimer = new QTimer(this);
  connect(animationTimer, &QTimer::timeout, this, &FpsChartRect::updateAnimation);
}

FpsChartRect::~FpsChartRect() {
  if(animationTimer) {
    animationTimer->stop();
    delete animationTimer;
  }
}

void FpsChartRect::createAppHost() {
  appHost = std::make_unique<AppHost>();
#ifdef __APPLE__
  auto defaultTypeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
  auto emojiTypeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
#else
  auto defaultTypeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
#endif
  appHost->addTypeface("default", defaultTypeface);
}

void FpsChartRect::setFpsValues(const QVector<float>& values) {
  if(_fpsValues != values) {
    _previousFpsValues = _fpsValues;
    _fpsValues = values;
    _animationProgress = 0.0f;
    startAnimation();
    update();
    Q_EMIT fpsValuesChanged();
  }
}

void FpsChartRect::setMinFps(float value) {
  if(!qFuzzyCompare(_minFps, value)) {
    _preMinFps = _minFps;
    _minFps = value;
    if(_animationProgress >= 1.0f) {
      _animationProgress = 0.0f;
      startAnimation();
    }
    update();
    Q_EMIT minFpsChanged();
  }
}

void FpsChartRect::setMaxFps(float value) {
  if(!qFuzzyCompare(_maxFps, value)) {
    _preMaxFps = _maxFps;
    _maxFps = value;
    if(_animationProgress >= 1.0f) {
      _animationProgress = 0.0f;
      startAnimation();
    }
    update();
    Q_EMIT maxFpsChanged();
  }
}

void FpsChartRect::setAvgFps(float value) {
  if (!qFuzzyCompare(_avgFps, value)) {
    _preAvgFps = _avgFps;
    _avgFps = value;
    if(_animationProgress >= 1.0f) {
      _animationProgress = 0.0f;
      startAnimation();
    }
    update();
    Q_EMIT avgFpsChanged();
  }
}

void FpsChartRect::draw() {
  auto device = tgfxWindow->getDevice();
  if(device == nullptr) {
    return;
  }

  auto context = device->lockContext();
  if(context == nullptr) {
    return;
  }

  auto surface = tgfxWindow->getSurface(context);
  if(surface == nullptr) {
    device->unlock();
    return;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  //drawRect(canvas, 0, 0, static_cast<float>(width()),static_cast<float>(height()),0xFF121212 );
  drawFps(canvas);
  canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}

void FpsChartRect::drawFps(tgfx::Canvas* canvas) {
  if(_fpsValues.isEmpty()) {
    return;
  }

  float minFps = getInterpolatedValue(_preMinFps, _minFps);
  float maxFps = getInterpolatedValue(_preMaxFps, _maxFps);
  float avgFps = getInterpolatedValue(_preAvgFps, _avgFps);

  float fpsMin = qMax(0.0f, minFps * 0.8f);
  float fpsMax = maxFps * 1.2f;
  float range = fpsMax - fpsMin;

  if(range <= 0.1f) {
    range = 60.0f;
  }

  auto chartWidth = static_cast<float>(width() - 60);
  auto chartHeight = static_cast<float>(height() - 50);
  float xStart = 40;
  float yStart = 10;

  drawLine(canvas, xStart, yStart, xStart, yStart + chartHeight, 0xFFAAAAAA);
  drawLine(canvas, xStart, yStart + chartHeight, xStart + chartWidth, yStart + chartHeight, 0xFFAAAAAA);

  QVector<float> interpolatedValues;
  int count = static_cast<int>(qMin(_fpsValues.size(), _previousFpsValues.size()));

  for(int i = 0; i < count; ++i) {
    float oldVal = _previousFpsValues[i];
    float newVal = _fpsValues[i];
    float interpolated = getInterpolatedValue(oldVal, newVal);
    interpolatedValues.append(interpolated);
  }

  for(int i = count; i < _fpsValues.size(); ++i) {
    interpolatedValues.append(_fpsValues[i] * _animationProgress);
  }

  if(!interpolatedValues.isEmpty()) {
    float xStep = chartWidth / interpolatedValues.size();
    float rectWidth = xStep - 1;

    if(rectWidth < 2) {
      rectWidth = 2;
    }

    if(xStep < rectWidth) {
      xStep = rectWidth;
    }

    float x = xStart;

    for(float fps : interpolatedValues) {
      uint32_t color;
      if(fps < 59.5f) {
        float intensity = qMin(1.0f, (60.0f - fps) / 30.0f);
        auto colorVal = static_cast<uint8_t>(155 + 100 * intensity);
        color = 0xFF000000 | (static_cast<uint32_t>(colorVal) << 16);
      }
      else if(fps > 60.5f) {
        float intensity = qMin(1.0f, (fps - 60.f) / 60.0f);
        auto colorVal = static_cast<uint8_t>(155 + 100 * intensity);
        color = 0xFF000000 | colorVal;
      }
      else {
        color = 0xFF00CC00;
      }

      float rectHeight = chartHeight * (fps - fpsMin) / (fpsMax - fpsMin);
      rectHeight = qMin(rectHeight, chartHeight);
      rectHeight = qMax(rectHeight, 1.0f);

      float y = yStart + chartHeight - rectHeight;
      drawRect(canvas, x, y, rectWidth, rectHeight, color);

      x += xStep;
    }
  }

  if(fpsMin < 60.0f && fpsMax > 60.0f) {
    float y60 = yStart + chartHeight * (1.0f - (60.0f - fpsMin) / (fpsMax - fpsMin));
    y60 = qBound(yStart, y60, yStart + chartHeight);

    tgfx::Paint linePaint;
    linePaint.setColor(getTgfxColor(0x8000FF00));
    linePaint.setStrokeWidth(1.0f);
    linePaint.setStyle(tgfx::PaintStyle::Stroke);

    drawLine(canvas, xStart, y60, xStart - chartWidth, y60, 0x8000FF00);
    drawTextContrast(canvas, appHost.get(), xStart - 35, y60 - 5, 0xFFFFCC44, "60 FPS");
  }

  if(avgFps > 0) {
    float avgY = yStart + chartHeight * (1.0f - (avgFps - fpsMin) / (fpsMax - fpsMin));
    avgY = qBound(yStart, avgY, yStart + chartHeight);

    drawLine(canvas, xStart, avgY, xStart + chartWidth, avgY, 0xFFFFCC44);
    drawTextContrast(canvas, appHost.get(), xStart - 35, avgY + 5, 0xFFFFCC44, "Avg");
  }

  float legendY = yStart + chartHeight + 25;

  drawRect(canvas, xStart, legendY, 12, 12, 0xFFCC0000);
  drawTextContrast(canvas, appHost.get(), xStart + 16, legendY, 0xFFFFFFFF, "< 60 FPS");

  drawRect(canvas, xStart + 100, legendY, 12, 12, 0xFF00CC00);
  drawTextContrast(canvas, appHost.get(), xStart + 116, legendY, 0xFFFFFFFF, "= 60 FPS");

  drawRect(canvas, xStart + 200, legendY, 12, 12, 0xFF0000CC);
  drawTextContrast(canvas, appHost.get(), xStart + 216, legendY, 0xFFFFFFFF, "> 60 FPS");

  canvas->save();
}

void FpsChartRect::startAnimation() {
  if(!animationTimer->isActive()) {
    animationTimer->start(16);
  }
}

void FpsChartRect::updateAnimation() {
  _animationProgress += 0.1f;
  if (_animationProgress >= 1.0f) {
    _animationProgress = 1.0f;
    animationTimer->stop();
  }
}

float FpsChartRect::getInterpolatedValue(float oldVal, float newVal) const {
  return oldVal + (_animationProgress * (newVal - oldVal));
}

QSGNode* FpsChartRect::updatePaintNode(QSGNode* oldNode,
                                       UpdatePaintNodeData*) {
  auto node = dynamic_cast<QSGImageNode*>(oldNode);
  if (!tgfxWindow) {
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
  }

  auto pixelRadio = window()->devicePixelRatio();
  auto screenWidth = static_cast<int>(ceil(width() * pixelRadio));
  auto screenHeight = static_cast<int>(ceil((float)height() * pixelRadio));
  auto sizeChanged =
      appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRadio));
  if (sizeChanged) {
    tgfxWindow->invalidSize();
  }
  draw();
  auto texture = tgfxWindow->getQSGTexture();
  if (texture) {
    if (node == nullptr) {
      node = window()->createImageNode();
    }

    node->setTexture(texture);
    node->markDirty(QSGNode::DirtyMaterial);
    node->setRect(boundingRect());
  }
  return node;
}





