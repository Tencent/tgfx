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

#include <qabstractitemmodel.h>
#include "SourceContents.h"
#include "TracyVector.hpp"
#include "ViewData.h"
#include "SourceView.h"
#include "FramesView.h"

class View;
class SourceView;
class FramesView;
class StatisticsModel : public QAbstractTableModel {
  Q_OBJECT
  Q_PROPERTY(QString totalZoneCount READ getTotalZoneCount NOTIFY zoneCountChanged)
  Q_PROPERTY(QString visibleZoneCount READ getVisibleZoneCount NOTIFY zoneCountChanged)
  Q_PROPERTY(bool rangeActive READ isRangeActive WRITE setRangeActive NOTIFY rangeActiveChanged)
  Q_PROPERTY(QString filterText READ getFilterText WRITE setFilterText NOTIFY filterTextChanged)
  Q_PROPERTY(int accumulationMode READ getAccumulationMode WRITE setAccumulationMode NOTIFY accumulationModeChanged)


 public:
  enum class AccumulationMode { SelfOnly, AllChildren, NonReentrantChildren };

  enum Column {
    NameColumn,
    LocationColumn,
    TotalTimeColumn,
    CountColumn,
    MtpcColumn,
    ThreadCountColumn,
    ColumnCount
  };

  enum StatMode {
    Instrumentation = 0,
    Sampling = 1,
    Gpu = 2,
  };

  enum Role {
    nameRole = Qt::UserRole + 1,
    locationRole = Qt::UserRole + 2,
    totalTimeRole = Qt::UserRole + 3,
    countRole = Qt::UserRole + 4,
    mtpcRole = Qt::UserRole + 5,
    threadCountRole = Qt::UserRole + 6,
    colorRole = Qt::UserRole + 7,
    percentageRole = Qt::UserRole + 8,
    totalTimeRawRole = Qt::UserRole + 9,
  };

  struct SrcLocZonesSlim {
    int16_t srcloc;
    uint16_t numThreads;
    size_t numZones;
    int64_t total;
  };

  struct StatCache {
    RangeSlim range;
    AccumulationMode accumulationMode;
    size_t srcCount = 0;
    size_t count = 0;
    int64_t total = 0;
    uint16_t threadNum = 0;
  };

  struct FrameRange {
    int start;
    int end;
  };

  explicit StatisticsModel(tracy::Worker* w, ViewData* vd, View* v, QObject* parent = nullptr);
  ~StatisticsModel();

  /////*column role & data*/////
  int rowCount(const QModelIndex& parent) const override;
  int columnCount(const QModelIndex& parent) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString getTotalZoneCount() const {
    return QString::number(totalZoneCount);
  }

  QString getVisibleZoneCount() const {
    return QString::number(srcData.size());
  }

  SourceContents getSource() const {
    return source;
  }

  const tracy::Vector<SrcLocZonesSlim>& getSrcData() const {
    return srcData;
  }

  QString getFilterText() const {
    return filterText;
  }

  QColor getTextColor() const {
    return QColor(255, 255,255, 230);
  }

  int getAccumulationMode() const {
    return static_cast<int>(statAccumulationMode) ;
  }

  StatMode statisticMode() const {
    return statisticsMode;
  }

  QString FilterText() const {
    return filterText;
  }

  /////*Icon utilities*/////
  uint32_t getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth) const;
  uint32_t getStrLocColor(const tracy::SourceLocation& srcloc, int depth) const;
  const tracy::SourceLocation& getSrcLocFromIndex(const QModelIndex& index) const;

  /////*data utilities*/////
  bool isZoneReentry(const tracy::ZoneEvent& zone) const;
  bool isZoneReentry(const tracy::ZoneEvent& zone, uint64_t tid) const;
  int64_t getZoneChildTimeFast(const tracy::ZoneEvent& zone);

  //////*view source*/////
  void openSource(const char* fileName, int line, const tracy::Worker* worker, const View* view);
  void parseSource(const char* fileName, const tracy::Worker* worker, const View* view);
  void viewSource(const char* fileName, int line);
  static bool srcFileValid(const char* fn, uint64_t olderThan, const tracy::Worker& worker, View* view);

  /////*range selected*/////
  bool isRangeActive() const;
  void setRangeActive(bool active);

  Q_SLOT void setFilterText(const QString& filter);
  Q_SLOT void refreshData();
  Q_SLOT void setStatRange(int startTime, int endTime, bool active);
  Q_SLOT void setStatisticsMode(StatMode mode);


  Q_SIGNAL void statisticsModeChanged();
  Q_SIGNAL void statisticsUpdated();
  Q_SIGNAL void zoneCountChanged();
  Q_SIGNAL void rangeActiveChanged();
  Q_SIGNAL void filterTextChanged();
  Q_SIGNAL void accumulationModeChanged();

  void RefreshFrameData();
  QVector<float>& getFps();
  QVector<float>& getDrawCall();
  QVector<float>& getTriangles();
  uint32_t getFirstFrame() const;
  uint32_t getLastFrame() const;

  Q_INVOKABLE void openSource(int row);
  Q_INVOKABLE void setAccumulationMode(int mode);
  Q_INVOKABLE void updateZoneCountLabels();
  Q_INVOKABLE void sort(int column, Qt::SortOrder order) override;
  Q_INVOKABLE void clearFilter();
  Q_INVOKABLE void refreshTableData();

 protected:
  void refreshInstrumentationData();
  void refreshSamplingData();
  void refreshGpuData();
  bool matchFilter(const QString& name, const QString& location) const;

 private:
  View* view = nullptr;
  ViewData* viewData = nullptr;
  tracy::Worker* worker = nullptr;
  FramesView* framesView = nullptr;
  const tracy::FrameData* frames = nullptr;
  QVector<float> fps;
  QVector<float> drawCall;
  QVector<float> triangle;

  tracy::Vector<SrcLocZonesSlim> srcData;
  tracy::unordered_flat_map<int16_t, StatCache> statCache;
  tracy::unordered_flat_map<uint32_t, uint32_t> sourceFiles;

  AccumulationMode statAccumulationMode;
  StatMode statisticsMode;
  SourceContents source;

  QString filterText;
  Qt::SortOrder sortOrder;

  Range stateRange;
  FrameRange frameRange;
  int statMode;
  int targetLine = 0;
  int selectedLine = 0;
  uint64_t targetAddr;
  size_t totalZoneCount;

  SourceView* srcView = nullptr;
  QString srcViewFile;
  QTimer* dataRefreshTimer;
};
