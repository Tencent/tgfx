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
#include <QObject>
#include "ViewData.h"
#include "Worker.h"

namespace inspector {
class SelectFrameModel : public QAbstractItemModel {
  Q_OBJECT
 public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    ValueRole,
  };

  explicit SelectFrameModel(Worker* worker, ViewData* viewData, QObject* parent = nullptr);

  Q_SLOT void refreshData();

  int rowCount(const QModelIndex& parent) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  QModelIndex index(int row, int column, const QModelIndex& parent) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int columnCount(const QModelIndex& parent) const override;

 private:
  struct Item {
    QString name;
    QVariant value;
  };

  Worker* worker;
  ViewData* viewData;
  QList<Item> items;
};
}  // namespace inspector