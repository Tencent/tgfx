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

#include "SelectFrameModel.h"
#include "TimePrint.h"

namespace inspector {
SelectFrameModel::SelectFrameModel(Worker* worker, ViewData* viewData, QObject* parent)
    : QAbstractItemModel(parent), worker(worker), viewData(viewData) {
  refreshData();
}

void SelectFrameModel::refreshData() {
  beginResetModel();
  items.clear();
  const auto& selectFrame = viewData->selectFrame;
  const auto& dataContext = worker->getDataContext();
  auto fps = 0;
  int64_t selectFrameTime = 0;
  int64_t drawCall = 0;
  int64_t triangles = 0;
  if (selectFrame > 1) {
    selectFrameTime = worker->getFrameTime(dataContext.frameData, selectFrame);
    fps = static_cast<int>(1000000000 / selectFrameTime);
    drawCall = worker->getFrameDrawCall(selectFrame);
    triangles = worker->getFrameTriangles(selectFrame);
  }
  auto frameItem = Item{tr("Frame"), selectFrame};
  auto timeItem = Item{tr("Time"), TimeToString(selectFrameTime)};
  auto fpsItem = Item{tr("FPS"), fps};
  auto drawCallItem = Item{tr("DrawCall"), drawCall};
  auto trianglesItem = Item{tr("Triangles"), triangles};
  items.append(frameItem);
  items.append(timeItem);
  items.append(fpsItem);
  items.append(drawCallItem);
  items.append(trianglesItem);
  endResetModel();
}

int SelectFrameModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(items.count());
}

QVariant SelectFrameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= items.count()) {
    return {};
  }

  const auto& item = items.at(index.row());
  switch (role) {
    case NameRole: {
      return item.name;
    }
    case ValueRole: {
      return item.value;
    }
    default: {
      return {};
    }
  }
}

QHash<int, QByteArray> SelectFrameModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[NameRole] = "name";
  roles[ValueRole] = "value";
  return roles;
}

QModelIndex SelectFrameModel::index(int row, int column, const QModelIndex& parent) const {
  return !parent.isValid() && row >= 0 && row < items.count() && column >= 0 && column < 2
             ? createIndex(row, column)
             : QModelIndex();
}

QModelIndex SelectFrameModel::parent(const QModelIndex& child) const {
  Q_UNUSED(child);
  return {};
}

int SelectFrameModel::columnCount(const QModelIndex& parent) const {
  return !parent.isValid() ? 2 : 0;
}
}  // namespace inspector