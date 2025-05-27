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
#include <unordered_set>
#include "AtttributeModel.h"

namespace inspector {
TaskTreeModel::TaskTreeModel(QObject* parent) : QAbstractItemModel(parent) {
}

TaskTreeModel::~TaskTreeModel() {
  deleteTree(rootItem);
}

QHash<int, QByteArray> TaskTreeModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[NameRole] = "name";
  roles[StartTimeRole] = "startTime";
  roles[EndTimeRole] = "endTime";
  roles[DurationRole] = "duration";
  roles[TypeRole] = "type";
  roles[OpIdRole] = "opId";
  roles[TaskIndexRole] = "taskIndex";
  return roles;
}

QVariant TaskTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || !rootItem) {
    return {};
  }

  auto& dataContext = worker->GetDataContext();
  auto& opTasks = dataContext.opTasks;
  auto item = static_cast<TaskItem*>(index.internalPointer());
  switch (role) {
    case NameRole:
      return item->getName();
    case StartTimeRole:
      return static_cast<qint64>(item->getOpData().start);
    case EndTimeRole:
      return static_cast<qint64>(item->getOpData().end);
    case DurationRole:
      return static_cast<qint64>(item->getOpData().end) -
             static_cast<qint64>(item->getOpData().start);
    case TypeRole:
      return static_cast<uint>(item->getType());
    case OpIdRole:
      return static_cast<uint>(item->getOpId());
    case TaskIndexRole: {
      const uint32_t opId = item->getOpId();
      for (size_t i = 0; i < opTasks.size(); ++i) {
        if (opTasks[i]->id == opId) {
          return static_cast<int>(i);
        }
      }
      return -1;
    }
    default:
      return {};
  }
}

QModelIndex TaskTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) {
    return {};
  }

  TaskItem* parentItem = nullptr;
  if (!parent.isValid()) {
    parentItem = rootItem;
  } else {
    parentItem = static_cast<TaskItem*>(parent.internalPointer());
  }
  auto childItem = parentItem->childAt(static_cast<uint32_t>(row));
  if (childItem) {
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

  return parentItem ? parentItem->childCount() : 0;
}

int TaskTreeModel::columnCount(const QModelIndex& parent) const {
  Q_UNUSED(parent)
  return 1;
}

bool TaskTreeModel::matchesFilter(const QString& name) const {
  if (!typeFilter.isEmpty() && !typeFilter.contains(name)) {
    return false;
  }

  if (!textFilter.isEmpty()) {
    QRegularExpression regex(textFilter, QRegularExpression::CaseInsensitiveOption);
    return regex.match(name).hasMatch();
  }

  return true;
}

void TaskTreeModel::deleteTree(TaskItem* root) {
  if (!root) {
    return;
  }

  for (auto child: root->childrenItems) {
    deleteTree(child);
  }
  delete root;
}

void TaskTreeModel::refreshData() {
  beginResetModel();

  auto selectFrame = viewData->selectFrame;
  const auto& frameData = worker->GetFrameData()->frames;
  if (selectFrame > frameData.size() || selectFrame < 2) {
    return;
  }
  const auto selectFrameData = frameData[selectFrame];
  auto& dataContext = worker->GetDataContext();
  auto& opTasks = dataContext.opTasks;
  auto opChilds = dataContext.opChilds;
  std::vector<std::shared_ptr<OpTaskData>> selectFrameOpTasks;
  for (const auto& iter : opTasks) {
    if (iter->start > selectFrameData.end) {
      break;
    }
    if (iter->start > selectFrameData.start) {
      selectFrameOpTasks.push_back(iter);
    }
  }
  deleteTree(rootItem);
  rootItem = processTaskLevel(selectFrameOpTasks, opChilds);
}

TaskItem* TaskTreeModel::processTaskLevel(
    const std::vector<std::shared_ptr<OpTaskData>>& opTasks,
    std::unordered_map<uint32_t, std::vector<unsigned int>> opChilds) {
  std::unordered_map<uint32_t, TaskItem*> nodeMap;
  for (auto& opTask: opTasks) {
    nodeMap[opTask->id] = new TaskItem(opTask.get());
  }
  std::unordered_set<uint32_t> childNodes;
  TaskItem* root = nullptr;
  for (const auto& pair: opChilds) {
    for (auto childIndex: pair.second) {
      childNodes.insert(childIndex);
    }
  }

  for (uint32_t i = 0; i < opChilds.size(); ++i) {
    if (childNodes.find(i) == childNodes.end()) {
      if (root == nullptr) {
        root = nodeMap[i];
      }
      else {
        auto emptyOpTaskData = new OpTaskData();
        root = new TaskItem(emptyOpTaskData);
        break;
      }
    }
  }

  if (root == nullptr) {
    auto emptyOpTaskData = new OpTaskData();
    root = new TaskItem(emptyOpTaskData);
  }
  for (const auto& pair: opChilds) {
    auto parendIndex = pair.first;
    auto& childIndices = pair.second;
    auto parentNode = nodeMap[parendIndex];
    for (auto childIndex: childIndices) {
      auto childNode = nodeMap[childIndex];
      childNode->setIndex(parentNode->childCount());
      parentNode->appendChild(childNode);
      childNode->setParent(parentNode);
    }
  }
  if (root->getType() == OpTaskType::Unknown) {
    for (uint32_t i = 0; i < opTasks.size(); ++i) {
      auto node = nodeMap[i];
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

  TaskItem* item = static_cast<TaskItem*>(index.internalPointer());
  if (item) {
    Q_EMIT taskSelected(item->getOpData(), item->getName(), item->getOpId());
  }
}

void TaskTreeModel::setAttributeModel(AtttributeModel* model) {
  if (model && model != atttributeModel) {
    atttributeModel = model;
    connect(this, &TaskTreeModel::taskSelected, atttributeModel,
            &AtttributeModel::updateSelectedTask);
  }
}
}  // namespace inspector
