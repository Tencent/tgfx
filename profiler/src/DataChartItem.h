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
  Q_PROPERTY(QString dataName WRITE setDataName)
  Q_PROPERTY(StatisticsModel* model WRITE setModel)
public:
  enum ChartType {
    Polyline,
    Line,
    Column
  };

  DataChartItem(QQuickItem* parent = nullptr, ChartType chartType = Line);

  virtual QVector<float>& getData();
  virtual uint32_t getMaxData(QVector<float>& data, uint32_t min, uint32_t max);

  void setModel(StatisticsModel* model) {
    this->model = model;
  }
  void setDataName(QString value) { name = std::move(value); }

  void createAppHost();
  void draw();
  void drawCoordinateAxes(tgfx::Canvas* canvas, float xStart, float yStart, float xLength, float yLength);
  void drawData(tgfx::Canvas* canvas);

  void drawChart(tgfx::Canvas* canvas, tgfx::Path& linePath, float xStart, float yStart, float width, float height);
  void drawPolylineChart(tgfx::Path& linePath, float xStart, float yStart, float width);
  void drawLineChart(tgfx::Path& linePath, float xStart, float yStart, float width);
  void drawColumChart(tgfx::Canvas* canvas, float xStart, float yStart, float width, float height);

  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
  void hoverMoveEvent(QHoverEvent* event) override;
  void hoverLeaveEvent(QHoverEvent* event) override;
private:
  QString name;
  ChartType chartType;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
public:
  StatisticsModel* model = nullptr;
};

class FPSChartItem: public DataChartItem
{
public:
  FPSChartItem(QQuickItem* parent = nullptr);

  QVector<float>& getData() override;
  uint32_t getMaxData(QVector<float>& data, uint32_t min, uint32_t max) override;
};

class DrawCallChartItem: public DataChartItem
{
public:
  DrawCallChartItem(QQuickItem* parent = nullptr);

  QVector<float>& getData() override;
};

class TriangleChartItem: public DataChartItem
{
public:
  TriangleChartItem(QQuickItem* parent = nullptr);

  QVector<float>& getData() override;
};