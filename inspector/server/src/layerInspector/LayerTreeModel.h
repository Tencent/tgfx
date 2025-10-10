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

#include <flatbuffers/flexbuffers.h>
#include "LayerModel.h"

namespace inspector {
class LayerTreeModel : public LayerModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(LayerTreeModel)
 public:
  Q_DISABLE_COPY_MOVE(LayerTreeModel)
  explicit LayerTreeModel(QObject* parent = nullptr);
  ~LayerTreeModel() override = default;
  void setLayerTreeData(const flexbuffers::Map& contentMap);
  bool selectLayer(uint64_t address);
  void selectDefaultLayer();
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  void flushLayerTree();
  Q_INVOKABLE void MouseSelectedIndex(QModelIndex index);
  Q_INVOKABLE void MouseHoveredIndex(QModelIndex index);
 signals:
  void selectIndex(QModelIndex index);
  void expandAllTree();
  void selectAddress(uint64_t address);
  void hoveredAddress(uint64_t address);
  void flushLayerTreeSignal();

 private:
  QModelIndex indexFromAddress(uint64_t address) const;
  void setupModelData(const flexbuffers::Map& contentMap, LayerItem* parent);
  QHash<uint64_t, LayerItem*> m_AddressToItem;
};

}  // namespace inspector