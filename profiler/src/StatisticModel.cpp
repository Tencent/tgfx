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

#include "StatisticModel.h"
#include <QRegularExpression>
#include <QTimer>
#include <src/profiler/TracyColor.hpp>
#include "TracyPrint.hpp"
#include "View.h"
#include "tracy_pdqsort.h"

StatisticsModel::StatisticsModel(tracy::Worker* w, ViewData* vd, View* v, QObject* parent)
    : QAbstractTableModel(parent), view(v), viewData(vd), worker(w),
      frames(worker->GetFramesBase()), statAccumulationMode(AccumulationMode::SelfOnly),
      statisticsMode(StatMode::Instrumentation), sortOrder(Qt::AscendingOrder), statMode(0),
      targetAddr(0), totalZoneCount(0) {
  dataRefreshTimer = new QTimer(this);
  connect(dataRefreshTimer, &QTimer::timeout, this, &StatisticsModel::refreshData);
  dataRefreshTimer->start(200);
  refreshData();
}




StatisticsModel::~StatisticsModel() {
  if(dataRefreshTimer) {
    dataRefreshTimer->stop();
  }
};

int StatisticsModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(srcData.size());
}

int StatisticsModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return Column::ColumnCount;
}

QHash<int, QByteArray> StatisticsModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[nameRole] = "Name";
  roles[locationRole] = "Location";
  roles[totalTimeRole] = "Totaltime";
  roles[countRole] = "Count";
  roles[mtpcRole] = "Mtpc";
  roles[threadCountRole] = "Threadcount";
  roles[colorRole] = "color";
  roles[percentageRole] = "percentage";
  roles[totalTimeRawRole] = "totalTimeRaw";
  return roles;
}

void StatisticsModel::openSource(int row) {
  auto& srcloc = getSrcLocFromIndex(index(row, StatisticsModel::LocationColumn));
  const char* fileName = worker->GetString(srcloc.file);
  int line = static_cast<int>(srcloc.line);

  viewSource(fileName, line);
}


void StatisticsModel::updateZoneCountLabels() {
  Q_EMIT zoneCountChanged();
}

QVariant StatisticsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || static_cast<size_t>(index.row()) >= srcData.size()) {
    return QVariant();
  }

  const auto idx = static_cast<size_t>(index.row());
  const auto& entry = srcData[idx];

  switch (role) {
    case nameRole : {
      auto& srcloc = worker->GetSourceLocation(entry.srcloc);
      auto name = worker->GetString(srcloc.name.active ? srcloc.name : srcloc.function);
      return QString::fromUtf8(name);
    }

    case locationRole : {
      auto& srcloc = worker->GetSourceLocation(entry.srcloc);
      QString location = worker->GetString(srcloc.file) + QStringLiteral(":") + QString::number(srcloc.line);
      return location;
    }

    case totalTimeRole : {
      double time = entry.total;
      return tracy::TimeToString(static_cast<int64_t>(time));
    }

    case countRole : {
      return QString::number(entry.numZones);
    }

    case mtpcRole : {
      if (entry.numZones == 0) return QStringLiteral("0ms");
      double mtpc = static_cast<double>(entry.total) / entry.numZones;
      return QString::fromUtf8(tracy::TimeToString(static_cast<int64_t>(mtpc)));
    }

    case threadCountRole :
      return QString::number(entry.numThreads);

    case colorRole : {
      auto& srcloc = worker->GetSourceLocation(entry.srcloc);
      QColor color = QColor::fromRgba(getStrLocColor(srcloc, 0));
      return color;
    }

    case percentageRole : {
      int64_t timeRange = 0;
      if (stateRange.active) {
        timeRange = stateRange.max - stateRange.min;
        if (timeRange == 0) {
          timeRange = 1;
        }
      } else {
        timeRange = worker->GetLastTime() - worker->GetFirstTime();
      }
      double percentage = (timeRange > 0) ? (entry.total * 100.0 / timeRange) : 0.0;
      return percentage;
    }

    case totalTimeRawRole : {
      return QVariant::fromValue(entry.total);
    }

    default:
      break;
  }

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case NameColumn: {
        auto& srcloc = worker->GetSourceLocation(entry.srcloc);
        auto name = worker->GetString(srcloc.name.active ? srcloc.name : srcloc.function);
        return name;
      }

      case LocationColumn: {
        auto& srcloc = worker->GetSourceLocation(entry.srcloc);
        QString location = worker->GetString(srcloc.file) + QString::number(srcloc.line);
        return location;
      }

      case TotalTimeColumn: {
        double time = entry.total;
        return tracy::TimeToString(static_cast<int64_t>(time));
      }

      case CountColumn: {
        return QString::number(entry.numZones);
      }

      case MtpcColumn: {
        if (entry.numZones == 0) {
          return "0ms";
        }
        double mtpc = static_cast<double>(entry.total) / entry.numZones;
        return tracy::TimeToString(static_cast<int64_t>(mtpc));
      }

      case ThreadCountColumn: {
        return QString::number(entry.numThreads);
      }
    }
  }
  return QVariant();
}

QVariant StatisticsModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
  switch (section) {
    case NameColumn:
      return tr("Name");

    case LocationColumn:
      return tr("Location");

    case TotalTimeColumn:
      return tr("Total Time");

    case CountColumn:
      return tr("Count");

    case ThreadCountColumn:
      return tr("Threads Counts");

    case MtpcColumn:
      return tr("MTPC");

    default:
      return QVariant();
  }
}

int64_t StatisticsModel::getZoneChildTimeFast(const tracy::ZoneEvent& zone) {
  int64_t time = 0;
  if (zone.HasChildren()) {
    auto& children = worker->GetZoneChildren(zone.Child());
    if (children.is_magic()) {
      auto& vec = *(tracy::Vector<tracy::ZoneEvent>*)&children;
      for (auto& v : vec) {
        assert(v.IsEndValid());
        time += v.End() - v.Start();
      }
    } else {
      for (auto& v : children) {
        assert(v->IsEndValid());
        time += v->End() - v->Start();
      }
    }
  }
  return time;
}

bool StatisticsModel::isZoneReentry(const tracy::ZoneEvent& zone) const {
#ifndef TRACY_NO_STATISTICS
  if (worker->AreSourceLocationZonesReady()) {
    auto& slz = worker->GetZonesForSourceLocation(zone.SrcLoc());
    if (!slz.zones.empty() && slz.zones.is_sorted()) {
      auto it = std::lower_bound(
          slz.zones.begin(), slz.zones.end(), zone.Start(),
          [](const auto& lhs, const auto& rhs) { return lhs.Zone()->Start() < rhs; });
      if (it != slz.zones.end() && it->Zone() == &zone) {
        return isZoneReentry(zone, worker->DecompressThread(it->Thread()));
      }
    }
  }
#endif
  for (const auto& thread : worker->GetThreadData()) {
    const tracy::ZoneEvent* parent = nullptr;
    const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>* timeline = &thread->timeline;
    if (timeline->empty()) continue;
    for (;;) {
      if (timeline->is_magic()) {
        auto vec = (tracy::Vector<tracy::ZoneEvent>*)timeline;
        auto it = std::upper_bound(vec->begin(), vec->end(), zone.Start(),
                                   [](const auto& l, const auto& r) { return l < r.Start(); });
        if (it != vec->begin()) {
          --it;
        }
        if (zone.IsEndValid() && it->Start() > zone.End()) {
          break;
        }
        if (it == &zone) {
          return false;
        }
        parent = it;
        if (parent->SrcLoc() == zone.SrcLoc()) {
          return true;
        }
        timeline = &worker->GetZoneChildren(parent->Child());
      } else {
        auto it = std::upper_bound(timeline->begin(), timeline->end(), zone.Start(),
                                   [](const auto& l, const auto& r) { return l < r->Start(); });
        if (it != timeline->begin()) {
          --it;
        }
        if (zone.IsEndValid() && (*it)->Start() > zone.End()) {
          break;
        }
        if (*it == &zone) {
          return false;
        }
        if (!(*it)->HasChildren()) {
          break;
        }
        parent = *it;
        timeline = &worker->GetZoneChildren(parent->Child());
      }
    }
  }
  return true;
}

bool StatisticsModel::isZoneReentry(const tracy::ZoneEvent& zone, uint64_t tid) const {
  const auto thread = worker->GetThreadData(tid);
  const tracy::ZoneEvent* parent = nullptr;
  const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>* timeline = &thread->timeline;
  if (timeline->empty()) return false;
  for (;;) {
    if (timeline->is_magic()) {
      auto vec = (tracy::Vector<tracy::ZoneEvent>*)timeline;
      auto it = std::upper_bound(vec->begin(), vec->end(), zone.Start(),
                                 [](const auto& l, const auto& r) { return l < r.Start(); });
      if (it != vec->begin()) {
        --it;
      }
      if (zone.IsEndValid() && it->Start() > zone.End()) {
        break;
      }
      if (it == &zone) {
        return false;
      }
      if (!it->HasChildren()) {
        break;
      }
      parent = it;
      timeline = &worker->GetZoneChildren(parent->Child());
    }
  }
  return false;
}

void StatisticsModel::openSource(const char* fileName, int line, const tracy::Worker* worker,
                                 const View* view) {
  targetLine = line;
  selectedLine = line;
  targetAddr = 0;
  sourceFiles.clear();

  parseSource(fileName, worker, view);
  assert(!source.empty());
}

void StatisticsModel::parseSource(const char* fileName, const tracy::Worker* worker,
                                  const View* view) {
  if (source.filename() != fileName) {
    source.Parse(fileName, worker, view);
  }
}

void StatisticsModel::setAccumulationMode(int mode) {
  auto newMode = static_cast<AccumulationMode>(mode);
  if (statAccumulationMode == newMode) {
    return;
  }
  statAccumulationMode = newMode;
  refreshData();
  Q_EMIT accumulationModeChanged();
}

bool StatisticsModel::isRangeActive() const {
  return view ? stateRange.active : false;
}

void StatisticsModel::setRangeActive(bool active) {
  if (!view) {
    return;
  }

  if (stateRange.active == active) {
    return;
  }
  stateRange.active = active;

  refreshData();
  updateZoneCountLabels();
  Q_EMIT rangeActiveChanged();
}

void StatisticsModel::setStatisticsMode(StatMode mode) {
  if (statisticsMode != mode) {
    statisticsMode = mode;
    refreshData();
    Q_EMIT statisticsModeChanged();
  }
}

void StatisticsModel::setFilterText(const QString& filter) {
  if (filterText != filter) {
    filterText = filter;
    refreshData();
    Q_EMIT filterTextChanged();
  }
}

bool StatisticsModel::matchFilter(const QString& name, const QString& location) const {
  if (!filterText.isEmpty()) {
    QStringList terms = filterText.split(' ', Qt::SkipEmptyParts);
    for (const QString& term : terms) {

      bool termMatch = false;
      bool isNegative = term.startsWith('-');
      QString searchTerm = isNegative ? term.mid(1) : term;

      bool isRegex = searchTerm.startsWith("/") && searchTerm.endsWith("/");
      if (isRegex) {
        searchTerm = searchTerm.mid(1, searchTerm.length() - 2);
        QRegularExpression regex(searchTerm, QRegularExpression::CaseInsensitiveOption);

        if (regex.match(name).hasMatch() || regex.match(location).hasMatch()) {
          termMatch = true;
        }
      } else {
        if (searchTerm.contains(':')) {
          QStringList fieldSearch = searchTerm.split(':');
          if (fieldSearch.size() == 2) {
            QString field = fieldSearch[0].toLower();
            QString value = fieldSearch[1];

            if (field == "name") {
              termMatch = name.contains(value, Qt::CaseInsensitive);
            } else if (field == "file" || field == "location") {
              termMatch = location.contains(value, Qt::CaseInsensitive);
            }
          }
        } else {
          termMatch = name.contains(searchTerm, Qt::CaseInsensitive) ||
                      location.contains(searchTerm, Qt::CaseInsensitive);
        }
      }
      if (isNegative && termMatch) {
        return false;
      }
      if (!isNegative && !termMatch) {
        return false;
      }
    }
  }
  return true;
}

void StatisticsModel::viewSource(const char* fileName, int line) {
  if(!fileName || !view) return;

  srcViewFile = fileName;
  openSource(fileName, line, worker, view);

  if(!srcView) {
    srcView = new SourceView(nullptr);
    srcView->setAttribute(Qt::WA_DeleteOnClose);
    srcView->setStyleSheet("background-color: #2D2D2D;");
    connect(srcView, &QObject::destroyed, this, [this](){srcView = nullptr;});
  }

  const auto& source = getSource();
  if(!source.empty()) {
    QString content = QString::fromStdString(std::string(source.data(), source.dataSize()));
    srcView->setWindowTitle(QString("Source: %1").arg(fileName));
    srcView->loadSource(content, line);
    srcView->show();
    srcView->raise();
    srcView->activateWindow();
  }
}

uint32_t StatisticsModel::getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth) const {
  auto namehash = srcloc.namehash;
  if (namehash == 0 && srcloc.function.active) {
    const auto f = worker->GetString(srcloc.function);
    namehash = static_cast<uint32_t>(tracy::charutil::hash(f));
    if (namehash == 0) {
      namehash++;
    }
    srcloc.namehash = namehash;
  }
  if (namehash == 0) {
    return tracy::GetHsvColor(uint64_t(&srcloc), depth);
  } else {
    return tracy::GetHsvColor(namehash, depth);
  }
}

uint32_t StatisticsModel::getStrLocColor(const tracy::SourceLocation& srcloc, int depth) const {
  const auto color = srcloc.color;
  if (color != 0 && !viewData->forceColors) {
    return color | 0xFF000000;
  }
  if (viewData->dynamicColors == 0) {
    return 0xFFCC5555;
  }
  return getRawSrcLocColor(srcloc, depth);
}

const tracy::SourceLocation& StatisticsModel::getSrcLocFromIndex(const QModelIndex& index) const {
  const auto& entry = srcData[static_cast<size_t>(index.row())];
  return worker->GetSourceLocation(entry.srcloc);
}

void StatisticsModel::refreshData() {
  if (!worker->HasData()) {
    beginResetModel();
    srcData.clear();
    endResetModel();
    return;
  }

  switch (statisticsMode) {
    case Instrumentation:
      if (!worker->AreSourceLocationZonesReady()) {
        beginResetModel();
        srcData.clear();
        endResetModel();
        return;
      }
      refreshInstrumentationData();
      break;

    case Sampling:
    case Gpu:
      break;
  }
}

void StatisticsModel::setStatRange(int startFrame, int endFrame, bool) {
  stateRange.min = worker->GetFrameBegin(*frames, size_t(startFrame));
  stateRange.max = worker->GetFrameEnd(*frames, size_t(endFrame));
  if (stateRange.active) {
    refreshInstrumentationData();
    updateZoneCountLabels();
  }
}

void StatisticsModel::refreshInstrumentationData() {
  tracy::Vector<SrcLocZonesSlim> srcloc;
  beginResetModel();
  srcData.clear();

  if (statMode == 0) {
    if (!worker->HasData() || !worker->AreSourceLocationZonesReady()) {
      beginResetModel();
      srcData.clear();
      endResetModel();
      return;
    }

    auto& slz = worker->GetSourceLocationZones();
    srcloc.reserve(slz.size());
    uint32_t slzcnt = 0;

    if (stateRange.active) {
      const auto min = stateRange.min;
      const auto max = stateRange.max;
      const auto st = max - min;

      for (auto it = slz.begin(); it != slz.end(); ++it) {
        if (it->second.total != 0 && it->second.min <= st) {
          auto& sl = worker->GetSourceLocation(it->first);
          QString name = worker->GetString(sl.name.active ? sl.name : sl.function);
          QString file = worker->GetString(sl.file);

          if (!matchFilter(name, file)) {
            auto cit = statCache.find(it->first);
            if (cit != statCache.end() && cit->second.range == stateRange &&
                cit->second.accumulationMode == statAccumulationMode &&
                cit->second.srcCount == it->second.zones.size()) {
              if (cit->second.count != 0) {
                slzcnt++;
                srcloc.push_back_no_space_check(SrcLocZonesSlim{
                    it->first, cit->second.threadNum, cit->second.count, cit->second.total});
              }
            } else {
              tracy::unordered_flat_set<uint16_t> threads;
              size_t cnt = 0;
              int64_t total = 0;

              for (auto& v : it->second.zones) {
                auto& z = *v.Zone();
                const auto start = z.Start();
                const auto end = z.End();
                if (start >= min && end <= max) {
                  const auto zt = end - start;
                  if (statAccumulationMode == AccumulationMode::SelfOnly) {
                    total += zt - getZoneChildTimeFast(z);
                    cnt++;
                    threads.emplace(v.Thread());
                  } else if (statAccumulationMode == AccumulationMode::AllChildren ||
                             !isZoneReentry(z)) {
                    total += zt;
                    cnt++;
                    threads.emplace(v.Thread());
                  }
                }
              }
              const auto threadNum = (uint16_t)threads.size();
              if (cnt != 0) {
                slzcnt++;
                srcloc.push_back_no_space_check(SrcLocZonesSlim{it->first, threadNum, cnt, total});
              }
              statCache[it->first] = StatCache{
                  RangeSlim{stateRange.min, stateRange.max, stateRange.active},
                  statAccumulationMode,
                  it->second.zones.size(),
                  cnt,
                  total,
                  threadNum};
            }
          } else {
            slzcnt++;
            auto cit = statCache.find(it->first);
            if (cit != statCache.end() && cit->second.range == stateRange &&
                cit->second.accumulationMode == statAccumulationMode &&
                cit->second.srcCount == it->second.zones.size()) {
              if (cit->second.count != 0) {
                srcloc.push_back_no_space_check(SrcLocZonesSlim{
                    it->first, cit->second.threadNum, cit->second.count, cit->second.total});
              }
            } else {
              tracy::unordered_flat_set<uint16_t> threads;
              size_t cnt = 0;
              int64_t total = 0;
              for (auto& v : it->second.zones) {
                auto& z = *v.Zone();
                const auto start = z.Start();
                const auto end = z.End();
                if (start >= min && end <= max) {
                  const auto zt = end - start;
                  if (statAccumulationMode == AccumulationMode::SelfOnly) {
                    total += zt - getZoneChildTimeFast(z);
                    cnt++;
                    threads.emplace(v.Thread());
                  } else if (statAccumulationMode == AccumulationMode::AllChildren ||
                             !isZoneReentry(z)) {
                    total += zt;
                    cnt++;
                    threads.emplace(v.Thread());
                  }
                }
              }
              const auto threadNum = (uint16_t)threads.size();
              if (cnt != 0) {
                srcloc.push_back_no_space_check(SrcLocZonesSlim{it->first, threadNum, cnt, total});
              }
              statCache[it->first] = StatCache{
                  RangeSlim{stateRange.min, stateRange.max, stateRange.active},
                  statAccumulationMode,
                  it->second.zones.size(),
                  cnt,
                  total,
                  threadNum};
            }
          }
        }
      }
    } else {
      for (auto it = slz.begin(); it != slz.end(); ++it) {
        if (it->second.total != 0) {
          slzcnt++;
          size_t count = 0;
          int64_t total = 0;
          switch (statAccumulationMode) {
            case AccumulationMode::SelfOnly:
              count = it->second.zones.size();
              total = it->second.selfTotal;
              break;
            case AccumulationMode::AllChildren:
              count = it->second.zones.size();
              total = it->second.total;
              break;
            case AccumulationMode::NonReentrantChildren:
              count = it->second.nonReentrantCount;
              total = it->second.nonReentrantTotal;
              break;
          }

          totalZoneCount = slzcnt;

          auto& sl = worker->GetSourceLocation(it->first);
          QString name = worker->GetString(sl.name.active ? sl.name : sl.function);
          QString file = worker->GetString(sl.file);

          if (matchFilter(name, file)) {
            srcloc.push_back_no_space_check(SrcLocZonesSlim{
                it->first, static_cast<uint16_t>(it->second.threadCnt.size()), count, total});
          }
        }
      }
    }
    totalZoneCount = slzcnt;
  }

  srcData = std::move(srcloc);
  endResetModel();
  Q_EMIT statisticsUpdated();
}

void StatisticsModel::sort(int column, Qt::SortOrder order) {
  if (srcData.size() < 2) {
    //endResetModel();
    return;
  }
  beginResetModel();

  auto ascendingOrder = (order == Qt::AscendingOrder);
  switch (column) {
    case Column::NameColumn:
      if (ascendingOrder) {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              return strcmp(worker->GetZoneName(worker->GetSourceLocation(lhs.srcloc)),
                            worker->GetZoneName(worker->GetSourceLocation(rhs.srcloc))) < 0;
            });
      } else {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              return strcmp(worker->GetZoneName(worker->GetSourceLocation(lhs.srcloc)),
                            worker->GetZoneName(worker->GetSourceLocation(rhs.srcloc))) > 0;
            });
      }
      break;

    case Column::LocationColumn: {
      if (ascendingOrder) {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              const auto& sll = worker->GetSourceLocation(lhs.srcloc);
              const auto& slr = worker->GetSourceLocation(rhs.srcloc);
              const auto cmp = strcmp(worker->GetString(sll.file), worker->GetString(slr.file));
              return cmp == 0 ? sll.line < slr.line : cmp < 0;
            });
      } else {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              const auto& sll = worker->GetSourceLocation(lhs.srcloc);
              const auto& slr = worker->GetSourceLocation(rhs.srcloc);
              const auto cmp = strcmp(worker->GetString(sll.file), worker->GetString(slr.file));
              return cmp == 0 ? sll.line > slr.line : cmp > 0;
            });
      }
      break;
    }

    case Column::TotalTimeColumn:
      if (ascendingOrder) {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.total > rhs.total; });
      } else {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.total < rhs.total; });
      }
      break;

    case Column::CountColumn:
      if (ascendingOrder) {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.numZones < rhs.numZones; });
      } else {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.numZones > rhs.numZones; });
      }
      break;
    //MTPC: total / numZones
    case Column::MtpcColumn:
      if (ascendingOrder) {
        tracy::pdqsort_branchless(srcData.begin(), srcData.end(),
                                  [](const auto& lhs, const auto& rhs) {
                                    return double(lhs.total) / double(lhs.numZones) <
                                           double(rhs.total) / double(rhs.numZones);
                                  });
      } else {
        tracy::pdqsort_branchless(srcData.begin(), srcData.end(),
                                  [](const auto& lhs, const auto& rhs) {
                                    return double(lhs.total) / double(lhs.numZones) >
                                           double(rhs.total) / double(rhs.numZones);
                                  });
      }
      break;

    case Column::ThreadCountColumn:
      if (ascendingOrder) {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.numThreads < rhs.numThreads; });
      } else {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.numThreads > rhs.numThreads; });
      }
      break;
    default:
      assert(false);
  }
  endResetModel();
}

void StatisticsModel::clearFilter() {
  setFilterText("");
}

void StatisticsModel::refreshTableData() {
  refreshData();
  updateZoneCountLabels();
}

void StatisticsModel::refreshFrameData() {
  auto total = static_cast<qsizetype>(worker->GetFrameCount(*frames));
  if (fps.size() == total) {
    return;
  }
  fps.clear();
  drawCall.clear();
  triangle.clear();
  fps.reserve(total);
  drawCall.reserve(total);
  triangle.reserve(total);
  for (size_t i = 0; i < static_cast<size_t>(total); ++i) {
    auto frameTime = worker->GetFrameTime(*frames, i);
    fps.push_back(1000000000.0f / frameTime);
    drawCall.push_back(static_cast<float>(worker->GetFrameDrawCall(*frames, i)));
    triangle.push_back(static_cast<float>(worker->GetFrameTrangles(*frames, i)));
  }
}

bool StatisticsModel::isRunning() {
  return worker->IsConnected();
}

QVector<float>& StatisticsModel::getFps() {
  refreshFrameData();
  return fps;
}

QVector<float>& StatisticsModel::getDrawCall() {
  refreshFrameData();
  return drawCall;
}

QVector<float>& StatisticsModel::getTriangles() {
  refreshFrameData();
  return triangle;
}

uint32_t StatisticsModel::getFirstFrame() const {
  if (stateRange.active) {
    auto range = worker->GetFrameRange(*frames, viewData->zvStart, viewData->zvEnd);
    return static_cast<uint32_t>(std::max(range.first, 0));
  }
  return 1;
}

uint32_t StatisticsModel::getLastFrame() const {
  if (stateRange.active) {
    auto range = worker->GetFrameRange(*frames, viewData->zvStart, viewData->zvEnd);
    return std::min(static_cast<uint32_t>(range.second - 1), static_cast<uint32_t>(fps.size() - 1));
  }
  return static_cast<uint32_t>(fps.size() - 1);
}

bool StatisticsModel::srcFileValid(const char* fn, uint64_t olderThan, const tracy::Worker& worker,
                                   View* view) {
  if (worker.GetSourceFileFromCache(fn).data != nullptr) return true;
  struct stat buf = {};
  if (stat(view->sourceSubstitution(fn), &buf) == 0 && (buf.st_mode & S_IFREG) != 0) {
    if (!view->validateSourceAge()) return true;
    return (uint64_t)buf.st_mtime < olderThan;
  }
  return false;
}




