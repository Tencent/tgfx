/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QProxyStyle>
#include <QPainter>
#include <unordered_map>
#include "HoverTreeView.h"
#include "generate/SerializationStructure_generated.h"

class LayerTreeView : public QWidget {
  Q_OBJECT
public:
  LayerTreeView(QWidget* parent = nullptr);
  void ProcessMessage(const tgfx::fbs::TreeNode* treeNode);
  void expandSelectedLayer(uint64_t address);
Q_SIGNALS:
  void hoverIndexChanged(uint64_t address);
  void clickedIndexChanged(uint64_t address);
private:
  void CreateWidget();
  void CreateLayout();
  void CreateConnect();
  void CreateModelData(const tgfx::fbs::TreeNode* treeNode, QStandardItem* parentItem);
  void expandParents(QTreeView *view, const QModelIndex &index);
private:
  HoverTreeView* m_TreeView;
  QStandardItemModel* m_StandardItemModel;
  QVBoxLayout* m_VLayout;
  std::unordered_map<uint64_t, QStandardItem*> m_StandardItemMap;
};
