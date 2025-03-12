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

#include <QAbstractItemModel>
#include "SourceContents.h"
#include "TracyVector.hpp"
#include "ViewData.h"

class View;
class StatisticsModel : public QAbstractTableModel {
  Q_OBJECT

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

  explicit StatisticsModel(tracy::Worker& w, ViewData& vd, View* v, QObject* parent = nullptr);
  ~StatisticsModel();

  //init row and column
  static constexpr int nameRole = Qt::UserRole + 1;
  static constexpr int locationRole = Qt::UserRole + 2;
  static constexpr int totalTimeRole = Qt::UserRole + 3;
  static constexpr int countRole = Qt::UserRole + 4;
  static constexpr int mtpcRole = Qt::UserRole + 5;
  static constexpr int threadCountRole = Qt::UserRole + 6;

  int rowCount(const QModelIndex& parent) const override;
  int columnCount(const QModelIndex& parent) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  void sort(int column, Qt::SortOrder order) override;
  QHash<int, QByteArray> roleNames() const override;

  size_t getTotalZoneCount() const {
    return totalZoneCount;
  }
  size_t getVisibleZoneCount() const {
    return srcData.size();
  }
  tracy::Worker& getWorker() const {
    return worker;
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
  int64_t getZoneChildTimeFast(const tracy::ZoneEvent& zone);
  uint32_t getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth);
  uint32_t getStrLocColor(const tracy::SourceLocation& srcloc, int depth);
  const tracy::SourceLocation& getSrcLocFromIndex(const QModelIndex& index) const;

  bool isZoneReentry(const tracy::ZoneEvent& zone) const;
  bool isZoneReentry(const tracy::ZoneEvent& zone, uint64_t tid) const;

  //source data...
  void openSource(const char* fileName, int line, const tracy::Worker& worker, const View* view);
  void parseSource(const char* fileName, const tracy::Worker& worker, const View* view);

  AccumulationMode accumulationMode() const {
    return statAccumulationMode;
  }
  StatMode statisticMode() const {
    return statisticsMode;
  }
  QString FilterText() const {
    return filterText;
  }

  Q_SLOT void setAccumulationMode(AccumulationMode mode);
  Q_SLOT void setStatisticsMode(StatMode mode);
  Q_SLOT void setFilterText(const QString& filter);
  Q_SLOT void refreshData();
  Q_SLOT void setStatRange(int64_t start, int64_t end, bool active);

  Q_SIGNAL void accumulationModeChanged();
  Q_SIGNAL void statisticsModeChanged();
  Q_SIGNAL void filterTextChanged();
  Q_SIGNAL void statisticsUpdated();


  //////*fps chart data*//////
  QVector<float> getFpsValues() const;
  float getMinFps() const;
  float getMaxFps() const;
  float getAvgFps() const;
  void resetFrameDataCache() {_frameDataCached = false;}

 protected:
  void refreshInstrumentationData();
  void refreshSamplingData();
  void refreshGpuData();
  bool matchFilter(const QString& name, const QString& location) const;

  void cacheFrameData() const;

 private:
  View* view;
  ViewData& viewData;
  tracy::Worker& worker;

  tracy::Vector<SrcLocZonesSlim> srcData;
  tracy::unordered_flat_map<int16_t, StatCache> statCache;
  tracy::unordered_flat_map<uint32_t, uint32_t> sourceFiles;

  AccumulationMode statAccumulationMode;
  StatMode statisticsMode;
  SourceContents source;

  QString filterText;
  Qt::SortOrder sortOrder;

  int statMode;
  int targetLine = 0;
  int selectedLine = 0;
  uint64_t targetAddr;
  size_t totalZoneCount;

  mutable QVector<float> _fpsValues;
  mutable bool _frameDataCached = false;

};
