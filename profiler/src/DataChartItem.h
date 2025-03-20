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
#pragma once
#include "StatisticModel.h"
#include "TracyWorker.hpp"
#include "Utility.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"

class DataChartItem : public QQuickItem
{
  Q_OBJECT
  Q_PROPERTY(QString dataName WRITE setDataName READ getDataName)
  Q_PROPERTY(StatisticsModel* model WRITE setModel READ getModel)
public:
  enum ChartType {
    Polyline,
    Line,
    Column
  };

  DataChartItem(QQuickItem* parent = nullptr, ChartType chartType = Line, float lineThickness = 1.f);

  virtual QVector<float>& getData();
  virtual uint32_t getColor() = 0;
  virtual uint32_t getMaxData(QVector<float>& data, uint32_t min, uint32_t max);

  void setModel(StatisticsModel* model) { this->model = model; }
  StatisticsModel* getModel() const { return model; }
  void setDataName(QString value) { name = std::move(value); }
  QString getDataName() const { return name; }

  void draw();
  void drawCoordinateAxes(tgfx::Canvas* canvas, float xStart, float yStart, float xLength, float yLength);
  void drawData(tgfx::Canvas* canvas);

  void drawChart(tgfx::Canvas* canvas, tgfx::Path& linePath, float xStart, float yStart, float width, float height);
  void drawPolylineChart(tgfx::Path& linePath, float xStart, float yStart, float width, float height);
  void drawLineChart(tgfx::Path& linePath, float xStart, float yStart, float width);
  void drawColumChart(tgfx::Path& linePath, float xStart, float yStart, float width, float height);

  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
  void hoverMoveEvent(QHoverEvent* event) override;
  void hoverLeaveEvent(QHoverEvent* event) override;
private:
  QString name;
  ChartType chartType;
  float thickness;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
public:
  StatisticsModel* model = nullptr;
};

class FPSChartItem: public DataChartItem
{
public:
  FPSChartItem(QQuickItem* parent = nullptr): DataChartItem(parent, Polyline, 0.f) {}

  QVector<float>& getData() override { return model->getFps(); }
  uint32_t getColor() override { return 0xFFFEBA00; }
  uint32_t getMaxData(QVector<float>&, uint32_t, uint32_t) override { return 240; }
};

class DrawCallChartItem: public DataChartItem
{
public:
  DrawCallChartItem(QQuickItem* parent = nullptr): DataChartItem(parent, Column, 0.f) {}

  QVector<float>& getData() override { return model->getDrawCall(); }
  uint32_t getColor() override { return 0xFF509E54; }
};

class TriangleChartItem: public DataChartItem {
public:
  TriangleChartItem(QQuickItem* parent = nullptr): DataChartItem(parent, Line) {}

  QVector<float>& getData() override { return model->getTriangles(); }
  uint32_t getColor() override { return 0xFF6EDAF4; }
};