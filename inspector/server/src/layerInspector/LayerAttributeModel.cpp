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

#include "LayerAttributeModel.h"

namespace inspector {
template <typename T>
LayerItem* SetSingleAttribute(LayerItem* parent, const char* key, T value, bool isExpandable,
                              bool isAddress, uint64_t objID, bool isRenderable,
                              bool isImage = false, uint64_t imageID = 0) {
  QVariantList var;
  QVariant ID;
  var << key << value << isExpandable << isAddress << objID << isRenderable << isImage << imageID;
  parent->appendChild(std::make_unique<LayerItem>(var, parent));
  return parent->child(parent->childCount() - 1);
}

LayerAttributeModel::LayerAttributeModel(QObject* parent) : LayerModel(parent) {
  currentExpandItem = rootItem.get();
  currentExpandItemindex = QModelIndex();
  currentRow = 0;
  m_CurrentLayerAddress = 0;
}

void LayerAttributeModel::setLayerAttribute(const flexbuffers::Map& map) {
  beginResetModel();
  rootItem = std::make_shared<LayerItem>(QVariantList{"LayerName", "LayerAddress"});
  ProcessLayerAttribute(map, rootItem.get());
  addressToLayerData[m_CurrentLayerAddress].layerItem = rootItem;
  endResetModel();
  emit modelReset();
}

void LayerAttributeModel::setLayerSubAttribute(const flexbuffers::Map& map) {
  auto size = map.Keys().size();
  beginInsertRows(currentExpandItemindex, 0, (int)(--size));
  ProcessLayerAttribute(map, currentExpandItem);
  endInsertRows();
  emit expandItemRow(currentRow);
}

QVariant LayerAttributeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole) return {};

  const auto* item = static_cast<const LayerItem*>(index.internalPointer());
  if (item->data(index.column()).userType() == QMetaType::Float) {
    auto floatitem = item->data(index.column()).toFloat();
    return QString::number(floatitem, 'f', 2);
  }
  if (index.column() == 1 && item->data(3).toBool()) {
    auto address = item->data(1).toULongLong();
    if (address) {
      return QString("0x%1").arg(address, 0, 16);
    } else {
      return "nullptr";
    }
  }
  return item->data(index.column());
}

bool LayerAttributeModel::isExpandable(const QModelIndex& index) {
  if (!index.isValid()) return false;
  const auto* item = static_cast<const LayerItem*>(index.internalPointer());
  return item->data(2).toBool();
}

bool LayerAttributeModel::isRenderable(const QModelIndex& index) {
  if (!index.isValid()) return false;
  const auto* item = static_cast<const LayerItem*>(index.internalPointer());
  return item->data(5).toBool();
}

bool LayerAttributeModel::isImage(const QModelIndex& index) {
  if (!index.isValid()) return false;
  const auto* item = static_cast<const LayerItem*>(index.internalPointer());
  return item->data(6).toBool();
}

void LayerAttributeModel::switchToLayer(uint64_t address) {
  m_CurrentLayerAddress = address;
  beginResetModel();
  rootItem = addressToLayerData[address].layerItem;
  endResetModel();
  for (const auto& rowData : addressToLayerData[address].rowDatas) {
    switch (rowData.op) {
      case RowOp::Collapse:
        emit collapseItemRow(rowData.row);
        break;
      case RowOp::Expand:
        emit expandItemRow(rowData.row);
        break;
    }
  }
  emit modelReset();
}

void LayerAttributeModel::flushTree() {
  if (m_CurrentLayerAddress) {
    if (isExistedInLayerMap(m_CurrentLayerAddress)) {
      addressToLayerData.erase(m_CurrentLayerAddress);
      emit flushLayerAttribute(m_CurrentLayerAddress);
    }
  }
}

void LayerAttributeModel::clearAttribute() {
  beginResetModel();
  rootItem = std::make_shared<LayerItem>(QVariantList{"LayerName", "LayerAddress"});
  endResetModel();
}

void LayerAttributeModel::expandSubAttribute(const QModelIndex& index, int row) {
  if (!index.isValid()) return;
  currentExpandItemindex = index;
  currentExpandItem = static_cast<LayerItem*>(index.internalPointer());
  currentRow = row;
  uint64_t objID = currentExpandItem->data(4).toULongLong();
  emit expandSubAttributeSignal(objID);
  addressToLayerData[m_CurrentLayerAddress].rowDatas.push_back({RowOp::Expand, row});
}

bool LayerAttributeModel::eyeButtonState(const QModelIndex& index) {
  if (!index.isValid()) return false;
  auto item = static_cast<LayerItem*>(index.internalPointer());
  return item->eyeButtonState();
}

void LayerAttributeModel::setEyeButtonState(bool state, const QModelIndex& index) {
  if (!index.isValid()) return;
  auto item = static_cast<LayerItem*>(index.internalPointer());
  item->setEyeButtonState(state);
  emit flushImageChild(item->data(4).toULongLong());
}

uint64_t LayerAttributeModel::imageID(const QModelIndex& index) {
  if (!index.isValid()) return false;
  auto item = static_cast<LayerItem*>(index.internalPointer());
  return item->data(7).toULongLong();
}

void LayerAttributeModel::DisplayImage(bool isVisible, const QModelIndex& index) {
  (void)isVisible;
  if (!index.isValid()) return;
  auto item = static_cast<LayerItem*>(index.internalPointer());
  uint64_t objID = item->data(4).toULongLong();
  beginInsertRows(index, 0, 0);
  SetSingleAttribute(item, "Image", 0, false, false, 0, false, true, objID);
  endInsertRows();
}

void LayerAttributeModel::collapseRow(int row) {
  addressToLayerData[m_CurrentLayerAddress].rowDatas.push_back({RowOp::Collapse, row});
}

void LayerAttributeModel::expandRow(int row) {
  addressToLayerData[m_CurrentLayerAddress].rowDatas.push_back({RowOp::Expand, row});
}

void LayerAttributeModel::ProcessLayerAttribute(const flexbuffers::Map& contentMap,
                                                LayerItem* item) {
  auto keys = contentMap.Keys();
  for (size_t i = 0; i < keys.size(); i++) {
    QString key = keys[i].AsString().str().c_str();
    QString name = key.split('_').back();
    auto valueMap = contentMap[key.toStdString()].AsMap();
    bool isExpandable = valueMap["IsExpandable"].AsBool();
    bool isAddress = valueMap["IsAddress"].AsBool();
    uint64_t objID;
    if (valueMap["objID"].GetType() == flexbuffers::FBT_NULL) {
      objID = 0;
    } else {
      objID = valueMap["objID"].AsUInt64();
    }
    bool isRenderable = valueMap["IsRenderableObj"].AsBool();
    LayerItem* currentItem = nullptr;
    switch (valueMap["Value"].GetType()) {
      case flexbuffers::FBT_UINT:
        currentItem =
            SetSingleAttribute(item, name.toStdString().c_str(), valueMap["Value"].AsUInt64(),
                               isExpandable, isAddress, objID, isRenderable);
        break;
      case flexbuffers::FBT_INT:
        currentItem =
            SetSingleAttribute(item, name.toStdString().c_str(), valueMap["Value"].AsInt64(),
                               isExpandable, isAddress, objID, isRenderable);
        break;
      case flexbuffers::FBT_FLOAT:
        currentItem =
            SetSingleAttribute(item, name.toStdString().c_str(), valueMap["Value"].AsFloat(),
                               isExpandable, isAddress, objID, isRenderable);
        break;
      case flexbuffers::FBT_STRING:
        currentItem = SetSingleAttribute(item, name.toStdString().c_str(),
                                         valueMap["Value"].AsString().c_str(), isExpandable,
                                         isAddress, objID, isRenderable);
        break;
      case flexbuffers::FBT_BOOL:
        currentItem =
            SetSingleAttribute(item, name.toStdString().c_str(), valueMap["Value"].AsBool(),
                               isExpandable, isAddress, objID, isRenderable);
        break;
      default:
        qDebug() << "Unknown value type!";
    }
    if (currentItem && isRenderable) {
      SetSingleAttribute(currentItem, "Image", 0, false, false, 0, false, true, objID);
    }
  }
}
}  // namespace inspector
