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

#include "LayerItem.h"

namespace inspector {
LayerItem::LayerItem(QVariantList data, LayerItem* parentItem)
    : m_itemData(std::move(data)), m_parentItem(parentItem) {
}

void LayerItem::appendChild(std::unique_ptr<LayerItem>&& child) {
  m_childItems.push_back(std::move(child));
}

LayerItem* LayerItem::child(int row) {
  return row >= 0 && row < childCount() ? m_childItems.at((uint64_t)row).get() : nullptr;
}

int LayerItem::childCount() const {
  return int(m_childItems.size());
}

int LayerItem::columnCount() const {
  return int(m_itemData.count());
}

QVariant LayerItem::data(int column) const {
  return m_itemData.value(column);
}

int LayerItem::row() const {
  if (m_parentItem == nullptr) return 0;
  const auto it = std::find_if(
      m_parentItem->m_childItems.cbegin(), m_parentItem->m_childItems.cend(),
      [this](const std::unique_ptr<LayerItem>& treeItem) { return treeItem.get() == this; });
  if (it != m_parentItem->m_childItems.cend()) {
    return (int)std::distance(m_parentItem->m_childItems.cbegin(), it);
  }
  Q_ASSERT(false);
  return -1;
}

LayerItem* LayerItem::parentItem() {
  return m_parentItem;
}
}  // namespace inspector