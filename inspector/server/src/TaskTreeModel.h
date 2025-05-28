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
#include "InspectorEvent.h"
#include "ViewData.h"
#include "Worker.h"

namespace inspector {

class AtttributeModel;
class TaskItem {
 public:
  explicit TaskItem(OpTaskData* opData)
      : opTaskData(opData) {
  }

  void appendChild(TaskItem* child) {
    return childrenItems.push_back(child);
  }

  void setParent(TaskItem* parent) {
    parentItem = parent;
  }

  TaskItem* childAt(uint32_t row) {
    return (row >= 0 && row < childrenItems.size()) ? childrenItems[row] : nullptr;
  }

  int childCount() const {
    return static_cast<int>(childrenItems.size());
  }

  TaskItem* getParentItem() const {
    return parentItem;
  }

  const QString getName() const {
    return "test";
  }

  const OpTaskData& getOpData() const {
    return *opTaskData;
  }

  int64_t startTime() const {
    return opTaskData->start;
  }

  int64_t endTime() const {
    return opTaskData->end;
  }

  uint8_t getType() const {
    return opTaskData->type;
  }

  uint32_t getOpId() const {
    return opTaskData->id;
  }

  void setIndex(int index) {
    this->index = index;
  }

  int row() const {
    return index;
  }

  int index = 0;
  OpTaskData* opTaskData;
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
    DurationRole,
    TypeRole,
    OpIdRole,
    TaskIndexRole
  };

  explicit TaskTreeModel(QObject* parent = nullptr);
  ~TaskTreeModel() override;

  ///* TreeModel *///
  QHash<int, QByteArray> roleNames() const override;
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
  TaskItem* processTaskLevel(const std::vector<std::shared_ptr<OpTaskData>>& opTasks,
                             std::unordered_map<uint32_t, std::vector<unsigned int>> opChilds);
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
