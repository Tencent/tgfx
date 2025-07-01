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
#include <QVariantList>

namespace inspector {
class LayerItem {
 public:
  explicit LayerItem(QVariantList data, LayerItem* parentItem = nullptr);

  void appendChild(std::unique_ptr<LayerItem>&& child);

  LayerItem* child(int row);
  int childCount() const;
  int columnCount() const;
  QVariant data(int column) const;
  int row() const;
  LayerItem* parentItem();
  void clear() {
    m_childItems.clear();
  }

  bool eyeButtonState() {
    return m_EyeButtonState;
  }
  void setEyeButtonState(bool state) {
    m_EyeButtonState = state;
  }

 private:
  std::vector<std::unique_ptr<LayerItem>> m_childItems;
  QVariantList m_itemData;
  LayerItem* m_parentItem;
  bool m_EyeButtonState = false;
};
}  // namespace inspector
