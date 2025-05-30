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
#include <QQmlEngine>
#include <QtWidgets/QTreeView>
#include "InspectorEvent.h"
#include "ViewData.h"
#include "Worker.h"

namespace inspector {

class AtttributeModel;
class TaskItem {
 public:
  explicit TaskItem(QVariantList data, uint32_t opId)
      :opId(opId), itemData(std::move(data))  {}

  void appendChild(TaskItem* child) {
    return childrenItems.push_back(child);
  }

  void setParent(TaskItem* parent) {
    parentItem = parent;
  }

  TaskItem* childAt(uint32_t row) {
    return (row >= 0 && row < childrenItems.size()) ? childrenItems[row] : nullptr;
  }

  int columnCount() const {
    return int(itemData.count());
  }

  int childCount() const {
    return static_cast<int>(childrenItems.size());
  }

  TaskItem* getParentItem() const {
    return parentItem;
  }

  QVariant data(int column) const {
    return itemData.value(column);
  }

  void setIndex(int index) {
    this->index = index;
  }

  int row() const {
    if (parentItem == nullptr)
      return 0;
    const auto it = std::find_if(parentItem->childrenItems.cbegin(), parentItem->childrenItems.cend(),
                                 [this](TaskItem* treeItem) {
                                     return treeItem == this;
                                 });

    if (it != parentItem->childrenItems.cend()) {
      return int(std::distance(parentItem->childrenItems.cbegin(), it));
    }
    Q_ASSERT(false); // should not happen
  }

  int index = 0;
  uint32_t opId;
  QVariantList itemData;
  TaskItem* parentItem = nullptr;
  std::vector<TaskItem*> childrenItems;
};

class TaskTreeModel : public QAbstractItemModel {
  Q_OBJECT
 public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    StartTimeRole,
    EndTimeRole,
    RowCountRole,
    DurationRole,
    TypeRole,
    OpIdRole,
    TaskIndexRole
  };

  explicit TaskTreeModel(Worker* worker, ViewData* viewData, QObject* parent = nullptr);
  ~TaskTreeModel() override;

  ///* TreeModel *///
  QVariant data(const QModelIndex& index, int role) const override;
  QModelIndex index(int row, int column, const QModelIndex& parent) const override;
  QModelIndex parent(const QModelIndex& index) const override;
  int rowCount(const QModelIndex& parent) const override;
  int columnCount(const QModelIndex& parent) const override;

  Q_INVOKABLE void deleteTree(TaskItem* root);
  Q_INVOKABLE void selectedTask(const QModelIndex& index);
  Q_INVOKABLE void setAttributeModel(AtttributeModel* model);

  Q_SLOT void refreshData();

  Q_SIGNAL void taskSelected(const OpTaskData& opData, const QString& name, uint32_t opId);
  Q_SIGNAL void filterChanged();

 protected:
  TaskItem* processTaskLevel(int64_t selectFrameTime,
                             const std::vector<std::shared_ptr<OpTaskData>>& opTasks,
                             const std::unordered_map<uint32_t, std::vector<unsigned int>>& opChilds);
  bool matchesFilter(const QString& name) const;

 private:
  Worker* worker = nullptr;
  ViewData* viewData = nullptr;
  TaskItem* rootItem = nullptr;
  AtttributeModel* atttributeModel = nullptr;
  QStringList typeFilter;
  QStringList typeName;
  QString textFilter;
  bool filterEnabled = false;
  QMap<uint32_t, TaskItem*> opIdNodeMap;
};

}  // namespace inspector
