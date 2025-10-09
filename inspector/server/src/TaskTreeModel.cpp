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

#include "TaskTreeModel.h"
#include <QRegularExpression>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include "AttributeModel.h"
#include "TimePrint.h"
namespace inspector {
TaskTreeModel::TaskTreeModel(Worker* worker, ViewData* viewData, QObject* parent)
    : QAbstractItemModel(parent), worker(worker), viewData(viewData) {
  refreshData();
}

TaskTreeModel::~TaskTreeModel() {
  deleteTree(rootItem);
}

QHash<int, QByteArray> TaskTreeModel::roleNames() const {
  QHash<int, QByteArray> names;
  names[NameRole] = "name";
  names[WeightRole] = "weight";
  names[CostTimeRole] = "costTime";
  return names;
}

QVariant TaskTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || rootItem == nullptr) {
    return {};
  }

  auto item = static_cast<TaskItem*>(index.internalPointer());
  switch (role) {
    case NameRole:
    case CostTimeRole:
    case WeightRole: {
      return item->data(index.column());
    }
    default:
      return {};
  }
}

QModelIndex TaskTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) {
    return {};
  }

  TaskItem* parentItem =
      parent.isValid() ? static_cast<TaskItem*>(parent.internalPointer()) : rootItem;
  if (auto childItem = parentItem->childAt(static_cast<uint32_t>(row))) {
    return createIndex(row, column, childItem);
  }
  return {};
}

QModelIndex TaskTreeModel::parent(const QModelIndex& index) const {
  if (!index.isValid()) {
    return {};
  }

  auto childItem = static_cast<TaskItem*>(index.internalPointer());
  auto parentItem = childItem->getParentItem();

  if (parentItem == rootItem || parentItem == nullptr) {
    return {};
  }
  return createIndex(parentItem->row(), 0, parentItem);
}

int TaskTreeModel::rowCount(const QModelIndex& parent) const {
  if (parent.column() > 0) {
    return 0;
  }

  TaskItem* parentItem;
  if (!parent.isValid()) {
    parentItem = rootItem;
  } else {
    parentItem = static_cast<TaskItem*>(parent.internalPointer());
  }
  if (!parentItem) {
    return 0;
  }
  return parentItem->childCount();
}

int TaskTreeModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return static_cast<TaskItem*>(parent.internalPointer())->columnCount();
  }
  return rootItem ? rootItem->columnCount() : 3;
}

bool TaskTreeModel::filterOpTasks(
    const OpTaskData* opTask, const std::unordered_map<uint32_t, std::vector<uint32_t>>& opChilds) {
  const auto& dataContext = worker->getDataContext();
  auto& opTaskName = OpTaskName[opTask->type];
  auto& opTasks = dataContext.opTasks;
  if (matchesFilter(opTaskName)) {
    return true;
  }
  auto childsIndx = opChilds.find(opTask->id);
  if (childsIndx != opChilds.end()) {
    for (auto opTaskIdx : childsIndx->second) {
      if (filterOpTasks(opTasks.at(opTaskIdx).get(), opChilds)) {
        return true;
      }
    }
  }
  return false;
}

bool TaskTreeModel::matchesFilter(const std::string& name) const {
  auto& filterName = viewData->opTaskFilterName;
  if (filterName.empty() || filterName == name) {
    return true;
  }
  QRegularExpression regex(filterName.c_str(), QRegularExpression::CaseInsensitiveOption);
  return regex.match(name.c_str()).hasMatch();
}

bool TaskTreeModel::matchesFilter(uint8_t type) const {
  return viewData->opTaskFilterType & 1 << (type - 1);
}

void TaskTreeModel::deleteTree(TaskItem* root) {
  if (!root) {
    return;
  }

  for (auto child : root->childrenItems) {
    deleteTree(child);
  }
  delete root;
}

void TaskTreeModel::refreshData() {
  beginResetModel();

  auto selectFrame = viewData->selectFrame;
  const auto& frameData = worker->getFrameData()->frames;
  if (selectFrame > frameData.size() || selectFrame < 2) {
    endResetModel();
    return;
  }
  const auto selectFrameData = frameData[selectFrame];
  auto nextFrame = selectFrame + 1;
  if (selectFrame == frameData.size()) {
    nextFrame = 0;
  }
  const auto& dataContext = worker->getDataContext();
  auto selectFrameTime = worker->getFrameTime(dataContext.frameData, selectFrame);
  auto& opTasks = dataContext.opTasks;
  auto& opChilds = dataContext.opChilds;
  std::unordered_map<uint32_t, std::vector<uint32_t>> selectChilds;
  std::vector<std::shared_ptr<OpTaskData>> selectFrameOpTasks;
  for (const auto& iter : opTasks) {
    if (nextFrame && iter->start > frameData[nextFrame].start) {
      break;
    }
    if (iter->start > selectFrameData.start) {
      selectFrameOpTasks.push_back(iter);
      auto opIter = opChilds.find(iter->id);
      if (opIter != opChilds.end()) {
        auto& tmp = opIter->second;
        selectChilds[iter->id] = tmp;
      }
    }
  }
  deleteTree(rootItem);
  rootItem = processTaskLevel(selectFrameTime, selectFrameOpTasks, selectChilds);

  endResetModel();
}

TaskItem* TaskTreeModel::processTaskLevel(
    int64_t selectFrameTime, const std::vector<std::shared_ptr<OpTaskData>>& opTasks,
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& opChilds) {
  TaskItem* root = nullptr;
  QVariantList emptyColumnData{tr("opTaskName"), tr("opTaskTime"), tr("opTaskWeight")};
  if (opTasks.empty()) {
    root = new TaskItem(emptyColumnData, static_cast<uint32_t>(0));
    return root;
  }

  auto getTaskTimeByIndex = [&](uint32_t index) {
    for (auto& task : opTasks)
      if (task->id == index) return task->end - task->start;
    return (int64_t)0;
  };
  std::unordered_map<uint32_t, TaskItem*> nodeMap;
  for (auto& opTask : opTasks) {
    auto opTaskName = OpTaskName[opTask->type];
    if (!(matchesFilter(opTask->type) && filterOpTasks(opTask.get(), opChilds))) {
      continue;
    }
    long long childTasksTime{0};
    if (opChilds.find(opTask->id) != opChilds.end()) {
      for (auto childTaskId : opChilds.at(opTask->id)) {
        childTasksTime += getTaskTimeByIndex(childTaskId);
      }
    }

    auto opTaskTime = opTask->end - opTask->start;
    char weight[16];
    snprintf(weight, sizeof(weight), "%.2f%%", opTaskTime / double(selectFrameTime) * 100.0f);
    QVariantList columnData;
    columnData << opTaskName;
    columnData << TimeToString(opTaskTime) + QString(" ") + QString(weight);
    columnData << TimeToString(opTaskTime - childTasksTime);
    nodeMap[opTask->id] = new TaskItem(columnData, opTask->id);
  }
  std::unordered_set<uint32_t> childNodes;
  for (const auto& pair : opChilds) {
    for (auto childIndex : pair.second) {
      childNodes.insert(childIndex);
    }
  }

  for (uint32_t i = 0; i < opChilds.size(); ++i) {
    if (childNodes.find(i) == childNodes.end()) {
      if (root == nullptr) {
        root = nodeMap[i];
      } else {
        root = new TaskItem(emptyColumnData, static_cast<uint32_t>(0));
        break;
      }
    }
  }

  if (root == nullptr) {
    root = new TaskItem(emptyColumnData, static_cast<uint32_t>(0));
  }
  for (const auto& pair : opChilds) {
    auto parendIndex = pair.first;
    if (nodeMap.find(parendIndex) == nodeMap.end()) {
      continue;
    }
    auto& childIndices = pair.second;
    auto parentNode = nodeMap[parendIndex];
    for (auto childIndex : childIndices) {
      if (nodeMap.find(childIndex) == nodeMap.end()) {
        continue;
      }
      auto childNode = nodeMap[childIndex];
      childNode->setIndex(parentNode->childCount());
      parentNode->appendChild(childNode);
      childNode->setParent(parentNode);
    }
  }
  if (root->opId == 0) {
    for (const auto& opTask : opTasks) {
      if (nodeMap.find(opTask->id) == nodeMap.end()) {
        continue;
      }
      auto node = nodeMap[opTask->id];
      if (node->getParentItem() == nullptr && node != root) {
        root->appendChild(node);
        node->setParent(root);
      }
    }
  }
  return root;
}

void TaskTreeModel::selectedTask(const QModelIndex& index) {
  if (!index.isValid() || !rootItem) {
    return;
  }

  auto item = static_cast<TaskItem*>(index.internalPointer());
  if (item) {
    viewData->selectOpTask = static_cast<int>(item->opId);
    Q_EMIT selectTaskOp();
  }
}
}  // namespace inspector
