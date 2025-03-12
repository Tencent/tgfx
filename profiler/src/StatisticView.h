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

#include <QQuickItem>
#include <QQuickPaintedItem>
#include "SourceView.h"
#include "StatisticDelegate.h"
#include "StatisticModel.h"
#include "View.h"

class View;
class FramesView;
class SourceView;
class StatisticsDelegate;
class StatisticsView : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(StatisticsModel* model READ getModel CONSTANT)
  Q_PROPERTY(StatisticsDelegate* delegate READ getDelegate CONSTANT)
  Q_PROPERTY(QString totalZoneCount READ getTotalZoneCount NOTIFY zoneCountChanged)
  Q_PROPERTY(QString visibleZoneCount READ getVisibleZoneCount NOTIFY zoneCountChanged)
  Q_PROPERTY(bool limitRangeActive READ isLimitRangeActive WRITE setLimitRangeActive NOTIFY limitRangeActiveChanged)
  Q_PROPERTY(QString filterText READ getFilterText WRITE setFilterText NOTIFY filterTextChanged)

 public:
  explicit StatisticsView(tracy::Worker& _worker, ViewData& _viewData, View* _view, FramesView* _framesView, SourceView* _srcView, QQuickItem* parent = nullptr);
  ~StatisticsView();

  StatisticsModel* getModel() const {
    return model;
  }

  StatisticsDelegate* getDelegate() const {
    return delegate;
  }

  QString getTotalZoneCount() const;
  QString getVisibleZoneCount() const;
  QString getFilterText() const;
  bool isLimitRangeActive() const;

  void setLimitRangeActive(bool active);
  void setFilterText(const QString &text);

  Q_INVOKABLE void openSource(int row);
  Q_INVOKABLE void setAccumulationMode(int mode);
  Q_INVOKABLE void updateZoneCountLabels();
  Q_INVOKABLE void sort(int column, Qt::SortOrder order);
  Q_INVOKABLE void clearFilter();
  Q_INVOKABLE QVector<float> getFpsValues () const;
  Q_INVOKABLE float getMinFps() const;
  Q_INVOKABLE float getMaxFps() const;
  Q_INVOKABLE float getAvgFps() const;
  Q_INVOKABLE void refreshFpsData();
  Q_INVOKABLE void refreshTableData();



  Q_SIGNAL void zoneCountChanged();
  Q_SIGNAL void limitRangeActiveChanged();
  Q_SIGNAL void filterTextChanged();
  Q_SIGNAL void fpsDataChanged();

  Q_SLOT void onStatRangeChanged(int64_t start, int64_t end, bool active);

protected:
  void viewSource(const char* fileName, int line);
  static bool srcFileValid(const char* fn, uint64_t olderThan, const tracy::Worker& worker, View* view);

 private:
  tracy::Worker& worker ;
  ViewData& viewData  ;
  View* view = nullptr;
  FramesView* framesView = nullptr;
  StatisticsModel* model = nullptr;
  StatisticsDelegate* delegate = nullptr;
  SourceView* srcView = nullptr;
  QString srcViewFile;
  bool isVisible = false;
  bool isInitialized = false;
  QTimer* fpsUpdateTimer;
  QTimer* dataRefreshTimer;
};


//////*fps chart paint*/////
class FpsChartItem : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QVector<float> fpsValues READ fpsValues WRITE setFpsValues NOTIFY fpsValuesChanged)
  Q_PROPERTY(float minFps READ minFps WRITE setMinFps NOTIFY minFpsChanged)
  Q_PROPERTY(float maxFps READ maxFps WRITE setMaxFps NOTIFY maxFpsChanged)
  Q_PROPERTY(float avgFps READ avgFps WRITE setAvgFps NOTIFY avgFpsChanged)

public:
  explicit FpsChartItem(QQuickItem* parent = nullptr);
  ~FpsChartItem();

  void createAppHost();

  QVector<float> fpsValues() const {return _fpsValues;}
  void setFpsValues(const QVector<float>& values);

  float minFps() const {return _minFps;}
  float maxFps() const {return _maxFps;}
  float avgFps() const {return _avgFps;}
  void setMinFps(float value);
  void setMaxFps(float value);
  void setAvgFps(float value);

  void draw();
  void drawFps(tgfx::Canvas* canvas);
  void startAnimation();
  void updateAnimation();
  float getInterpolatedValue(float oldVal, float newVal) const;

  Q_SIGNAL void fpsValuesChanged();
  Q_SIGNAL void minFpsChanged();
  Q_SIGNAL void maxFpsChanged();
  Q_SIGNAL void avgFpsChanged();

protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
  QVector<float> _fpsValues;
  QVector<float> _previousFpsValues;
  QTimer* animationTimer;
  float _animationProgress = 1.0f;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
  float _minFps = 0.0f;
  float _maxFps = 0.0f;
  float _avgFps = 0.0f;
  float _preMinFps = 0.0f;
  float _preMaxFps = 0.0f;
  float _preAvgFps = 0.0f;
  float viewOffset = 0.0f;
};

//////*Fps bar chart*///////
class FpsChartRect : public QQuickItem {
  Q_OBJECT
 Q_PROPERTY(QVector<float> fpsValues READ fpsValues WRITE setFpsValues NOTIFY fpsValuesChanged)
 Q_PROPERTY(float minFps READ minFps WRITE setMinFps NOTIFY minFpsChanged)
 Q_PROPERTY(float maxFps READ maxFps WRITE setMaxFps NOTIFY maxFpsChanged)
 Q_PROPERTY(float avgFps READ avgFps WRITE setAvgFps NOTIFY avgFpsChanged)

public:
  explicit FpsChartRect(QQuickItem* parent = nullptr);
  ~FpsChartRect();

  void createAppHost();

  QVector<float> fpsValues() const {return _fpsValues;}
  void setFpsValues(const QVector<float>& values);

  float minFps() const {return _minFps;}
  float maxFps() const {return _maxFps;}
  float avgFps() const {return _avgFps;}
  void setMinFps(float value);
  void setMaxFps(float value);
  void setAvgFps(float value);

  void draw();
  void drawFps(tgfx::Canvas* canvas);
  //void drawFpsRect(tgfx::Canvas* canvas);
  void startAnimation();
  void updateAnimation();
  float getInterpolatedValue(float oldVal, float newVal) const;

  Q_SIGNAL void fpsValuesChanged();
  Q_SIGNAL void minFpsChanged();
  Q_SIGNAL void maxFpsChanged();
  Q_SIGNAL void avgFpsChanged();

protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
  QVector<float> _fpsValues;
  QVector<float> _previousFpsValues;
  QTimer* animationTimer;
  float _animationProgress = 1.0f;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
  float _minFps = 0.0f;
  float _maxFps = 0.0f;
  float _avgFps = 0.0f;
  float _preMinFps = 0.0f;
  float _preMaxFps = 0.0f;
  float _preAvgFps = 0.0f;
  float viewOffset = 0.0f;
};



