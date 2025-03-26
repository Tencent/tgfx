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

#include "LayerTreeView.h"
#include "CustomDelegate.h"
#include <QHeaderView>


LayerTreeView::LayerTreeView(QWidget* parent)
  :QWidget(parent)
{
  setAttribute(Qt::WA_StyledBackground);
  setStyleSheet("background-color : white;");
  CreateWidget();
  CreateLayout();
  CreateConnect();
}

void LayerTreeView::expandSelectedLayer(uint64_t address)
{
  m_TreeView->collapseAll();
  QModelIndex index = m_StandardItemMap[address]->index();
  expandParents(m_TreeView, index);
  //m_TreeView->expand(index);
  m_TreeView->scrollTo(index);
  auto selectionModel = m_TreeView->selectionModel();
  selectionModel->select(index,
    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  selectionModel->setCurrentIndex(index, QItemSelectionModel::Select);
}

void LayerTreeView::ProcessMessage(const tgfx::fbs::TreeNode* treeNode) {
  m_StandardItemMap.clear();
  m_StandardItemModel->clear();
  m_StandardItemModel->setHorizontalHeaderLabels({"LayerTree"});
  QStandardItem* itemRoot = m_StandardItemModel->invisibleRootItem();
  CreateModelData(treeNode, itemRoot);
  m_TreeView->viewport()->update();
}

void LayerTreeView::CreateWidget() {
  m_TreeView = new HoverTreeView(this);
  m_StandardItemModel = new QStandardItemModel(this);
  m_StandardItemModel->setHorizontalHeaderLabels({"LayerTree"});
  m_TreeView->setModel(m_StandardItemModel);
  m_TreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_TreeView->setStyleSheet(R"(
    QTreeView::branch:has-siblings:!adjoins-item {
      border-image: url(:/icons/vline.png) 0;
    }

    QTreeView::branch:has-siblings:adjoins-item {
      border-image: url(:/icons/branch-more.png) 0;
    }

    QTreeView::branch:!has-children:!has-siblings:adjoins-item {
        border-image: url(:/icons/branch-end.png) 0;
    }

    QTreeView::branch:has-children:!has-siblings:closed,
    QTreeView::branch:closed:has-children:has-siblings {
            border-image: none;
            image: url(:/icons/branch-closed.png);
    }

    QTreeView::branch:open:has-children:!has-siblings,
    QTreeView::branch:open:has-children:has-siblings  {
            border-image: none;
            image: url(:/icons/branch-open.png);
    }
    QTreeView::item::hover{
            background-color: #FFD700
    }
    )");
  m_TreeView->header()->setStyleSheet(
      "QHeaderView::section {"
      "  color: #000000;"
      "  font-size: 20pt;"
      "  font-family: Arial;"
      "  background-color: white;"
      "  border: 2px solid gray;"
      "  padding: 4px;"
      "}"
  );

  m_TreeView->setItemDelegate(new CustomDelegate);
  m_TreeView->setAlternatingRowColors(true);
  QPalette palette = m_TreeView->palette();
  // palette.setColor(QPalette::Base, QColor(45, 45, 54));
  // palette.setColor(QPalette::AlternateBase, QColor(32, 32, 32));
  m_TreeView->setPalette(palette);
  m_TreeView->viewport()->setAttribute(Qt::WA_Hover);
}

void LayerTreeView::CreateLayout() {
  m_VLayout = new QVBoxLayout(this);
  m_VLayout->setContentsMargins(0, 0, 0, 0);
  m_VLayout->addWidget(m_TreeView);
}

void LayerTreeView::CreateConnect() {
  connect(m_TreeView, &HoverTreeView::hoverIndexChanged, [this](const QModelIndex& index) {
    auto address = index.data(Qt::UserRole).toULongLong();
    emit hoverIndexChanged(address);
    qDebug() << "Hovered: " << address;
  });

  connect(m_TreeView, &QTreeView::clicked, [this](const QModelIndex& index) {
    auto address = index.data(Qt::UserRole).toULongLong();
    emit clickedIndexChanged(address);
    qDebug() << "Clicked: " << address;
  });
}

void LayerTreeView::CreateModelData(const tgfx::fbs::TreeNode* treeNode, QStandardItem* parentItem) {
  QString name = treeNode->name()->c_str();
  uint64_t address = treeNode->address();
  qDebug() << name;
  QStandardItem* item = new QStandardItem();
  item->setData(name, Qt::DisplayRole);
  item->setData(address, Qt::UserRole);
  item->setText(QString("%1: (0x%2)").arg(item->data(Qt::DisplayRole).toString()).
    arg(item->data(Qt::UserRole).toULongLong(), 0, 16));
  parentItem->appendRow(item);

  m_StandardItemMap.insert({address, item});

  for(auto child : *treeNode->children()) {
    CreateModelData(child, item);
  }
}

void LayerTreeView::expandParents(QTreeView* view, const QModelIndex& index) {
  QModelIndex parent = index.parent();
  while (parent.isValid()) {
    view->expand(parent);
    parent = parent.parent();
  }
}