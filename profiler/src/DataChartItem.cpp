////////////////////////////////////////////////////////////////////////////////////////////////
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
#include "DataChartItem.h"
#include <QSGImageNode>
#include <QToolTip>

DataChartItem::DataChartItem(QQuickItem* parent, ChartType chartType, float lineThickness)
  : QQuickItem(parent), chartType(chartType), thickness(lineThickness), appHost(AppHostInstance::GetAppHostInstance()) {
  setFlag(ItemHasContents, true);
  setFlag(ItemAcceptsInputMethod, true);
  setFlag(ItemIsFocusScope, true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
  setAntialiasing(true);
}

QVector<float>& DataChartItem::getData() {
  return model->getFps();
}

uint32_t DataChartItem::getMaxData(QVector<float>& data, uint32_t min, uint32_t max) {
  if (data.empty()) {
    return 0.f;
  }
  auto maxData = data[min];
  for (uint32_t i = min + 1; i <= max; ++i) {
    if (data[i] > maxData) {
      maxData = data[i];
    }
  }
  return static_cast<uint32_t>(maxData * 3 / 2);
}

void DataChartItem::drawCoordinateAxes(tgfx::Canvas* canvas, float xStart, float yStart, float xLength, float yLength) {
  uint32_t color = 0xFF4D4D4D;
  drawRect(canvas, xStart, yStart, xLength, yLength, color, 1.f);
}

void DataChartItem::drawChart(tgfx::Canvas*, tgfx::Path& linePath, float xStart, float yStart, float width, float height) {
  switch (chartType) {
    case Polyline: {
      drawPolylineChart(linePath, xStart, yStart, width, height);
      break;
    }
    case Line: {
      drawLineChart(linePath, xStart, yStart, width);
      break;
    }
    case Column: {
      drawColumChart(linePath, xStart, yStart, width, height);
      break;
    }
  }
}

void DataChartItem::drawPolylineChart(tgfx::Path& linePath, float xStart, float yStart, float width, float height) {
  if (xStart == 0) {
    if (thickness == 0.f) {
      linePath.moveTo(xStart + width / 2, height + yStart);
    }
    else {
      linePath.moveTo(xStart + width / 2, yStart);
      return;
    }
  }
  linePath.lineTo(xStart + width / 2, yStart);
}

void DataChartItem::drawLineChart(tgfx::Path& linePath, float xStart, float yStart, float width) {
  float lineWidth = width / 3;
  if (xStart == 0) {
    linePath.moveTo(xStart, yStart);
    linePath.lineTo(xStart + lineWidth * 2, yStart);
  }
  linePath.lineTo(xStart + lineWidth, yStart);
  linePath.lineTo(xStart + lineWidth * 2, yStart);
}

void DataChartItem::drawColumChart(tgfx::Path& linePath, float xStart, float yStart, float width, float height) {
  if (xStart == 0) {
    linePath.moveTo(xStart, height + yStart);
    linePath.lineTo(xStart, yStart);
  }
  else {
    linePath.lineTo(xStart, yStart);
  }
  linePath.lineTo(xStart + width, yStart);
}

void DataChartItem::drawData(tgfx::Canvas* canvas) {
  auto& data = getData();
  auto minX = model->getFirstFrame();
  auto maxX = model->getLastFrame();
  auto allData = maxX - minX + 1;
  auto maxY = getMaxData(data, minX, maxX);
  auto charWidth = static_cast<float>(width());
  auto charHeight = static_cast<float>(height());
  auto minRunningWidth = 10.f;
  auto maxShowData = static_cast<uint32_t>(charWidth / minRunningWidth);
  auto dataWidth = std::min(minRunningWidth, charWidth / allData);
  auto total = static_cast<uint32_t>(data.size());
  if (model->isRunning() && maxX > maxShowData) {
    minX = std::max(minX, maxX - maxShowData + 1);
    dataWidth = minRunningWidth;
  }
  float xStart = 0.f;
  float yStart = 0.f;
  float frameStart = 0.f;

  drawCoordinateAxes(canvas, xStart, yStart, charWidth, charHeight);
  uint32_t group = 1;
  group = std::max(group, static_cast<uint32_t>(1 / dataWidth));
  uint32_t onScreen = maxX - minX + 1;
  uint32_t i = 0;
  uint32_t idx = 0;
  tgfx::Path linePath;
  while (i < onScreen && minX + idx < total) {
    auto d = data[minX + idx];
    if (group > 1) {
      for (uint32_t j = 1; j < group; ++j) {
        d = std::max(d, data[minX + idx + j]);
      }
    }
    const auto currentHeight = std::min(static_cast<float>(maxY), d) / maxY * (charHeight - 2);
    const auto dataHeight = std::max(1.f, currentHeight);
    drawChart(canvas, linePath, frameStart + i * dataWidth, charHeight - dataHeight, dataWidth, dataHeight);
    ++i;
    idx += group;
  }
  if (!linePath.isEmpty()) {
    if (thickness == 0.f) {
      linePath.lineTo(frameStart + i * dataWidth, charHeight);
      linePath.close();
    }
    drawPath(canvas, linePath, getColor(), thickness);
  }
}

void DataChartItem::draw() {
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
  canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
  drawData(canvas);
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}

QSGNode* DataChartItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
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

void DataChartItem::hoverMoveEvent(QHoverEvent* event) {
  if (model == nullptr) {
    return QQuickItem::hoverMoveEvent(event);
  }
  auto& data = getData();
  auto minX = model->getFirstFrame();
  auto maxX = model->getLastFrame();
  auto maxY = getMaxData(data, minX, maxX);
  auto charWidth = static_cast<float>(width());
  auto minRunningWidth = 10.f;
  auto maxShowData = static_cast<uint32_t>(charWidth / minRunningWidth);
  auto dataWidth = std::min(minRunningWidth, charWidth / (maxX - minX + 1));
  if (model->isRunning() && maxX > maxShowData) {
    minX = std::max(minX, maxX - maxShowData + 1);
    dataWidth = minRunningWidth;
  }
  auto mouseX = static_cast<float>(event->position().x());
  auto sel = minX + static_cast<uint32_t>(mouseX / dataWidth);
  if (sel > maxX || sel < 0) {
    QToolTip::hideText();
    return QQuickItem::hoverMoveEvent(event);
  }
  uint32_t group = 1;
  group = std::max(group, static_cast<uint32_t>(1 / dataWidth));
  QString text;
  auto d = data[sel];
  if (group > 1) {
    auto g = std::min(group, maxY - sel);
    for (uint32_t j = 1; j < g; ++j) {
      d = std::max(d, data[sel + j]);
    }
    text = QString("Frames: %1 - %2(%3)\nMax%4: %5\n")
            .arg(sel)
            .arg(sel + g -1)
            .arg(g)
            .arg(name)
            .arg(d);
  }
  else {
    text = QString("Frames: %1\n%2: %3")
      .arg(sel)
      .arg(name)
      .arg(d);
  }
  QPoint globalPos = QCursor::pos();
  QToolTip::showText(globalPos, text, nullptr);
  return QQuickItem::hoverMoveEvent(event);
}

void DataChartItem::hoverLeaveEvent(QHoverEvent* event) {
  QToolTip::hideText();
  return QQuickItem::hoverLeaveEvent(event);
}
