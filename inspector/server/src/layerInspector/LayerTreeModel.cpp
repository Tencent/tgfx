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

#include "LayerTreeModel.h"

namespace inspector {
LayerTreeModel::LayerTreeModel(QObject* parent) : LayerModel(parent) {
}

void LayerTreeModel::setLayerTreeData(const flexbuffers::Map& contentMap) {
  beginResetModel();
  m_AddressToItem.clear();
  rootItem->clear();
  setupModelData(contentMap, rootItem.get());
  endResetModel();
  emit expandAllTree();
}

bool LayerTreeModel::selectLayer(uint64_t address) {
  QModelIndex index = indexFromAddress(address);
  if (index.isValid()) {
    emit selectIndex(index);
    return true;
  }
  return false;
}

QVariant LayerTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole) return {};

  const auto* item = static_cast<const LayerItem*>(index.internalPointer());
  bool convertFlag = false;
  auto address = item->data(index.column()).toULongLong(&convertFlag);
  if (convertFlag) {
    if (address != 0) return QString("0x%1").arg(address, 0, 16);
    else
      return QString("nullptr");
  }
  return item->data(index.column());
}

void LayerTreeModel::flushLayerTree() {
  emit flushLayerTreeSignal();
}

void LayerTreeModel::MouseSelectedIndex(QModelIndex index) {
  if (index.isValid()) {
    auto item = static_cast<LayerItem*>((index.internalPointer()));
    uint64_t address = item->data(1).toULongLong();
    emit selectAddress(address);
  }
}

void LayerTreeModel::MouseHoveredIndex(QModelIndex index) {
  if (index.isValid()) {
    auto item = static_cast<LayerItem*>((index.internalPointer()));
    emit hoveredAddress(item->data(1).toULongLong());
  }
}

QModelIndex LayerTreeModel::indexFromAddress(uint64_t address) const {
  auto item = m_AddressToItem.value(address, nullptr);
  if (!item || item == rootItem.get()) return QModelIndex();

  int row = item->row();
  return createIndex(row, 0, item);
}

void LayerTreeModel::setupModelData(const flexbuffers::Map& contentMap, LayerItem* parent) {
  QString name = contentMap["LayerType"].AsString().c_str();
  uint64_t address = contentMap["Address"].AsUInt64();
  QVariantList var = {name, address};
  parent->appendChild(std::make_unique<LayerItem>(var, parent));
  m_AddressToItem.insert(address, parent->child(parent->childCount() - 1));
  auto children = contentMap["Children"].AsVector();
  size_t childrenSize = children.size();
  for (size_t i = 0; i < childrenSize; i++) {
    auto childMap = children[i].AsMap();
    setupModelData(childMap, parent->child(parent->childCount() - 1));
  }
}
}  // namespace inspector