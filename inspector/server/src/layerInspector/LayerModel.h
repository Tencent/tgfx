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

#include <qqmlintegration.h>
#include <QModelIndex>
#include "LayerItem.h"

namespace inspector {
class LayerModel : public QAbstractItemModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(LayerModel)
 public:
  Q_DISABLE_COPY_MOVE(LayerModel)
  explicit LayerModel(QObject* parent = nullptr);
  ~LayerModel() override;

  QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

 protected:
  std::shared_ptr<LayerItem> rootItem;
};
}  // namespace inspector
