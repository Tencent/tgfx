#pragma once

#include "TracyVector.hpp"
#include "SourceContents.h"
#include "ViewData.h"


class StatisticsModel : public QAbstractTableModel {
  Q_OBJECT

public:
  enum class AccumulationMode {
    SelfOnly,
    AllChildren,
    NonReentrantChildren
  };

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
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role ) const override;
  void sort(int column, Qt::SortOrder order) override;

  size_t getTotalZoneCount() const {return totalZoneCount;}
  size_t getVisibleZoneCount() const {return srcData.size();}
  tracy::Worker& getWorker() const {return worker;}
  SourceContents getSource() const {return source;}
  const tracy::Vector<SrcLocZonesSlim>& getSrcData() const {return srcData;}
  int64_t getZoneChildTimeFast(const tracy::ZoneEvent& zone);
  uint32_t getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth);
  uint32_t getStrLocColor(const tracy::SourceLocation& srcloc, int depth);
  const tracy::SourceLocation& getSrcLocFromIndex(const QModelIndex& index) const;

  bool isZoneReentry(const tracy::ZoneEvent& zone) const;
  bool isZoneReentry(const tracy::ZoneEvent& zone, uint64_t tid) const;

  //source data...
  void openSource(const char* fileName, int line, const tracy::Worker& worker, const View* view);
  void parseSource(const char* fileName, const tracy::Worker& worker, const View* view);

  AccumulationMode accumulationMode() const {return statAccumulationMode;}
  StatMode statisticMode() const {return statisticsMode;}
  QString FilterText() const {return filterText;}

  Q_SLOT void setAccumulationMode(AccumulationMode mode);
  Q_SLOT void setStatisticsMode(StatMode mode);
  Q_SLOT void setFilterText(const QString& filter);
  Q_SLOT void refreshData();
  Q_SLOT void setStatRange(int64_t start, int64_t end, bool active);

  Q_SIGNAL void accumulationModeChanged();
  Q_SIGNAL void statisticsModeChanged();
  Q_SIGNAL void filterTextChanged();
  Q_SIGNAL void statisticsUpdated();

protected:
  void refreshInstrumentationData();
  void refreshSamplingData();
  void refreshGpuData();
  bool matchFilter(const QString&  name, const QString& location) const;

private:
  View* view;
  ViewData& viewData;
  tracy::Worker& worker;

  tracy::Vector<SrcLocZonesSlim> srcData;
  tracy::unordered_flat_map<int16_t,StatCache> statCache;
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
};
