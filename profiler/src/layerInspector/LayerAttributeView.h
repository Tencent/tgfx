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
#include <QWidget>
#include <QTreeView>

#include "generate/SerializationStructure_generated.h"

class LayerAttributeView : public QWidget{
  Q_OBJECT
public:
  LayerAttributeView(QWidget* parent = nullptr);
  void ProcessMessage(const tgfx::fbs::Layer* layer);
  uint64_t GetSelectedAddress() const {
    return m_SelectedLayerAddress;
  }
private:
  void CreateWidget();
  void CreateLayout();
  void CreateConnect();
  void ProcessLayerCommonAttribute(const tgfx::fbs::LayerCommonAttribute* commonAttribute,
    QStandardItem* itemRoot);
  void ProcessImageLayerAttribute(const tgfx::fbs::ImageLayerAttribute* imageLayerAttribute,
    QStandardItem* itemRoot);
  void ProcessShapeLayerAttribute(const tgfx::fbs::ShapeLayerAttribute* shapeLayerAttribute,
    QStandardItem* itemRoot);
  void ProcessSolidLayerAttribute(const tgfx::fbs::SolidLayerAttribute* solidLayerAttribute,
    QStandardItem* itemRoot);
  void ProcessTextLayerAttribute(const tgfx::fbs::TextLayerAttribute* textLayerAttribute,
    QStandardItem* itemRoot);

  void ProcessLayerStyleAttribute(const tgfx::fbs::LayerStyle* layerStyle, QStandardItem* parent);
  void ProcessLayerStyleCommonAttribute(const tgfx::fbs::LayerStyleCommonAttribute* commonAttribute, QStandardItem* parent);

  void ProcessLayerFilterAttribute(const tgfx::fbs::LayerFilter* layerFilter, QStandardItem* parent);
  void ProcessLayerFilterCommonAttribute(const tgfx::fbs::LayerfilterCommonAttribute* commonAttribute, QStandardItem* parent);

  void ProcessImageAttribute(const tgfx::fbs::ImageAttribute* imageAttribute, QStandardItem* parent);

  void ProcessShapeStyleAttribute(const tgfx::fbs::ShapeStyle* shapeStyle, QStandardItem* parent);

  void ProcessGradientAttribute(const tgfx::fbs::GradientAttribute* gradientAttribute, QStandardItem* parent);

  void ProcessShapeStyleCommonAttribute(const tgfx::fbs::ShapeStyleCommonAttribute* shapeStyleAttribute, QStandardItem* parent);

  void ProcessColorAttribute(const tgfx::fbs::Color* color, QStandardItem* parent);
private:
  QTreeView* m_TreeView;
  QStandardItemModel* m_StandardItemModel;
  QVBoxLayout* m_VLayout;
  uint64_t m_SelectedLayerAddress;
};