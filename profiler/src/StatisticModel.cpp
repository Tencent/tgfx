#include "StatisticModel.h"
#include <QRegularExpression>
#include <src/profiler/TracyColor.hpp>
#include "TracyPrint.hpp"
#include "View.h"
#include "tracy_pdqsort.h"

StatisticsModel::StatisticsModel(tracy::Worker& w, ViewData& vd, View* v, QObject* parent)
    : QAbstractTableModel(parent), view(v), viewData(vd), worker(w),
      statAccumulationMode(AccumulationMode::SelfOnly), statisticsMode(StatMode::Instrumentation),
      sortOrder(Qt::AscendingOrder), statMode(0), targetAddr(0), totalZoneCount(0) {
  refreshData();
}

StatisticsModel::~StatisticsModel() = default;

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

QVariant StatisticsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || static_cast<size_t>(index.row()) >= srcData.size()) {
    return QVariant();
  }

  const auto idx = static_cast<size_t>(index.row());
  const auto& entry = srcData[idx];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case NameColumn: {
        auto& srcloc = worker.GetSourceLocation(entry.srcloc);
        auto name = worker.GetString(srcloc.name.active ? srcloc.name : srcloc.function);
        return name;
      }

      case LocationColumn: {
        auto& srcloc = worker.GetSourceLocation(entry.srcloc);
        QString location = worker.GetString(srcloc.file) + QString::number(srcloc.line);
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
    auto& children = worker.GetZoneChildren(zone.Child());
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
  if (worker.AreSourceLocationZonesReady()) {
    auto& slz = worker.GetZonesForSourceLocation(zone.SrcLoc());
    if (!slz.zones.empty() && slz.zones.is_sorted()) {
      auto it = std::lower_bound(
          slz.zones.begin(), slz.zones.end(), zone.Start(),
          [](const auto& lhs, const auto& rhs) { return lhs.Zone()->Start() < rhs; });
      if (it != slz.zones.end() && it->Zone() == &zone) {
        return isZoneReentry(zone, worker.DecompressThread(it->Thread()));
      }
    }
  }
#endif
  for (const auto& thread : worker.GetThreadData()) {
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
        timeline = &worker.GetZoneChildren(parent->Child());
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
        if (parent->SrcLoc() == zone.SrcLoc()) {
          return true;
        }
        timeline = &worker.GetZoneChildren(parent->Child());
      }
    }
  }
  return true;
}

bool StatisticsModel::isZoneReentry(const tracy::ZoneEvent& zone, uint64_t tid) const {
  const auto thread = worker.GetThreadData(tid);
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
      if (parent->SrcLoc() == zone.SrcLoc()) {
        return true;
      }
      timeline = &worker.GetZoneChildren(parent->Child());
    }
  }
  return false;
}

void StatisticsModel::openSource(const char* fileName, int line, const tracy::Worker& worker,
                                 const View* view) {
  targetLine = line;
  selectedLine = line;
  targetAddr = 0;
  sourceFiles.clear();

  parseSource(fileName, worker, view);
  assert(!source.empty());
}

void StatisticsModel::parseSource(const char* fileName, const tracy::Worker& worker,
                                  const View* view) {
  if (source.filename() != fileName) {
    source.Parse(fileName, worker, view);
  }
}

void StatisticsModel::setAccumulationMode(AccumulationMode mode) {
  if (statAccumulationMode != mode) {
    statAccumulationMode = mode;
    refreshData();
    Q_EMIT accumulationModeChanged();
  }
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
      if (term.isEmpty()) {
        continue;
      }

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

uint32_t StatisticsModel::getRawSrcLocColor(const tracy::SourceLocation& srcloc, int depth) {
  auto namehash = srcloc.namehash;
  if (namehash == 0 && srcloc.function.active) {
    const auto f = worker.GetString(srcloc.function);
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

uint32_t StatisticsModel::getStrLocColor(const tracy::SourceLocation& srcloc, int depth) {
  const auto color = srcloc.color;
  if (color != 0 && !viewData.forceColors) {
    return color | 0xFF000000;
  }
  if (viewData.dynamicColors == 0) {
    return 0xFFCC5555;
  }
  return getRawSrcLocColor(srcloc, depth);
}

const tracy::SourceLocation& StatisticsModel::getSrcLocFromIndex(const QModelIndex& index) const {
  const auto& entry = srcData[static_cast<size_t>(index.row())];
  return worker.GetSourceLocation(entry.srcloc);
}

void StatisticsModel::refreshData() {
  if (!worker.HasData()) {
    beginResetModel();
    srcData.clear();
    endResetModel();
    return;
  }

  switch (statisticsMode) {
    case Instrumentation:
      if (!worker.AreSourceLocationZonesReady()) {
        beginResetModel();
        srcData.clear();
        endResetModel();
        return;
      }
      refreshInstrumentationData();
      break;

    case Sampling:
      refreshSamplingData();
      break;

    case Gpu:
      refreshGpuData();
      break;
  }
}

void StatisticsModel::setStatRange(int64_t start, int64_t end, bool active) {

  view->m_statRange.active = active;
  if (view->m_statRange.active && view->m_statRange.min == 0 && view->m_statRange.max == 0) {
    view->m_statRange.min = start;
    view->m_statRange.max = end;
  }
  refreshInstrumentationData();
}

void StatisticsModel::refreshInstrumentationData() {
  tracy::Vector<SrcLocZonesSlim> srcloc;
  beginResetModel();
  srcData.clear();

  if (statMode == 0) {
    if (!worker.HasData() || !worker.AreSourceLocationZonesReady()) {
      beginResetModel();
      srcData.clear();
      endResetModel();
      return;
    }

    auto& slz = worker.GetSourceLocationZones();
    srcloc.reserve(slz.size());
    uint32_t slzcnt = 0;

    if (view->m_statRange.active) {
      const auto min = view->m_statRange.min;
      const auto max = view->m_statRange.max;
      const auto st = max - min;

      for (auto it = slz.begin(); it != slz.end(); ++it) {
        if (it->second.total != 0 && it->second.min <= st) {
          auto& sl = worker.GetSourceLocation(it->first);
          QString name = worker.GetString(sl.name.active ? sl.name : sl.function);
          QString file = worker.GetString(sl.file);

          if (!matchFilter(name, file)) {
            auto cit = statCache.find(it->first);
            if (cit != statCache.end() && cit->second.range == view->m_statRange &&
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
                  RangeSlim{view->m_statRange.min, view->m_statRange.max, view->m_statRange.active},
                  statAccumulationMode,
                  it->second.zones.size(),
                  cnt,
                  total,
                  threadNum};
            }
          } else {
            slzcnt++;
            auto cit = statCache.find(it->first);
            if (cit != statCache.end() && cit->second.range == view->m_statRange &&
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
                  RangeSlim{view->m_statRange.min, view->m_statRange.max, view->m_statRange.active},
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

          auto& sl = worker.GetSourceLocation(it->first);
          QString name = worker.GetString(sl.name.active ? sl.name : sl.function);
          QString file = worker.GetString(sl.file);

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

void StatisticsModel::refreshSamplingData() {
  //todo
}

void StatisticsModel::refreshGpuData() {
  //todo
}

void StatisticsModel::sort(int column, Qt::SortOrder order) {
  if (srcData.size() < 2) {
    endResetModel();
    return;
  }
  beginResetModel();

  auto ascendingOrder = (order == Qt::AscendingOrder);
  switch (column) {
    case Column::NameColumn:
      if (ascendingOrder) {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              return strcmp(worker.GetZoneName(worker.GetSourceLocation(lhs.srcloc)),
                            worker.GetZoneName(worker.GetSourceLocation(rhs.srcloc))) < 0;
            });
      } else {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              return strcmp(worker.GetZoneName(worker.GetSourceLocation(lhs.srcloc)),
                            worker.GetZoneName(worker.GetSourceLocation(rhs.srcloc))) > 0;
            });
      }
      break;

    case Column::LocationColumn: {
      if (ascendingOrder) {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              const auto& sll = worker.GetSourceLocation(lhs.srcloc);
              const auto& slr = worker.GetSourceLocation(rhs.srcloc);
              const auto cmp = strcmp(worker.GetString(sll.file), worker.GetString(slr.file));
              return cmp == 0 ? sll.line < slr.line : cmp < 0;
            });
      } else {
        tracy::pdqsort_branchless(
            srcData.begin(), srcData.end(), [this](const auto& lhs, const auto& rhs) {
              const auto& sll = worker.GetSourceLocation(lhs.srcloc);
              const auto& slr = worker.GetSourceLocation(rhs.srcloc);
              const auto cmp = strcmp(worker.GetString(sll.file), worker.GetString(slr.file));
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