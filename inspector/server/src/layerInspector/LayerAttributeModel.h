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
enum class RowOp { Expand, Collapse };

struct RowData {
  RowOp op;
  int row;
};

struct LayerData {
  std::shared_ptr<LayerItem> layerItem;
  std::vector<RowData> rowDatas;
};

class LayerAttributeModel : public LayerModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(LayerAttributeModel)
 public:
  Q_DISABLE_COPY_MOVE(LayerAttributeModel)
  explicit LayerAttributeModel(QObject* parent = nullptr);
  ~LayerAttributeModel() override = default;
  void setLayerAttribute(const flexbuffers::Map& map);
  void setLayerSubAttribute(const flexbuffers::Map& map);
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  void SetCurrentAddress(uint64_t address) {
    m_CurrentLayerAddress = address;
  }
  uint64_t GetCurrentAddress() const {
    return m_CurrentLayerAddress;
  }
  bool isExistedInLayerMap(uint64_t address) const {
    return addressToLayerData.find(address) != addressToLayerData.end();
  }
  void switchToLayer(uint64_t address);
  void flushTree();
  void clearAttribute();
  Q_INVOKABLE bool eyeButtonState(const QModelIndex& index);
  Q_INVOKABLE void setEyeButtonState(bool state, const QModelIndex& index);
  Q_INVOKABLE uint64_t imageID(const QModelIndex& index);
  Q_INVOKABLE bool isExpandable(const QModelIndex& index);
  Q_INVOKABLE bool isRenderable(const QModelIndex& index);
  Q_INVOKABLE bool isImage(const QModelIndex& index);
  Q_INVOKABLE void expandSubAttribute(const QModelIndex& index, int row);
  Q_INVOKABLE void DisplayImage(bool isVisible, const QModelIndex& index);
  Q_INVOKABLE void collapseRow(int row);
  Q_INVOKABLE void expandRow(int row);
 signals:
  void expandSubAttributeSignal(uint64_t id);
  void expandItemRow(int row);
  void collapseItemRow(int row);
  void flushLayerAttribute(uint64_t address);
  void modelReset();
  void flushImageChild(uint64_t objID);

 private:
  void ProcessLayerAttribute(const flexbuffers::Map& contentMap, LayerItem* item);

 private:
  uint64_t m_CurrentLayerAddress;
  LayerItem* currentExpandItem;
  QModelIndex currentExpandItemindex;
  int currentRow;
  std::unordered_map<uint64_t, LayerData> addressToLayerData;
};
}  // namespace inspector
