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

#include "LayerModel.h"

namespace inspector {
LayerModel::LayerModel(QObject* parent)
    : QAbstractItemModel(parent),
      rootItem(std::make_shared<LayerItem>(QVariantList{"LayerName", "LayerAddress"})) {
}

LayerModel::~LayerModel() = default;

QModelIndex LayerModel::index(int row, int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) return {};

  LayerItem* parentItem =
      parent.isValid() ? static_cast<LayerItem*>(parent.internalPointer()) : rootItem.get();

  if (auto* childItem = parentItem->child(row)) return createIndex(row, column, childItem);
  return {};
}

QModelIndex LayerModel::parent(const QModelIndex& child) const {
  if (!child.isValid()) {
    return {};
  }

  auto childItem = static_cast<LayerItem*>(child.internalPointer());
  LayerItem* parentItem = childItem->parentItem();

  if (parentItem != rootItem.get()) {
    return createIndex(parentItem->row(), 0, parentItem);
  }
  return QModelIndex();
}

int LayerModel::rowCount(const QModelIndex& parent) const {
  if (parent.column() > 0) return 0;

  const LayerItem* parentItem =
      parent.isValid() ? static_cast<const LayerItem*>(parent.internalPointer()) : rootItem.get();

  return parentItem->childCount();
}

int LayerModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) return static_cast<LayerItem*>(parent.internalPointer())->columnCount();
  return rootItem->columnCount();
}

Qt::ItemFlags LayerModel::flags(const QModelIndex& index) const {
  return index.isValid() ? QAbstractItemModel::flags(index) : Qt::ItemFlags(Qt::NoItemFlags);
}

QVariant LayerModel::headerData(int section, Qt::Orientation orientation, int role) const {
  return orientation == Qt::Horizontal && role == Qt::DisplayRole ? rootItem->data(section)
                                                                  : QVariant{};
}
}  // namespace inspector