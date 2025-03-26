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
#include "LayerModel.h"

class LayerAttributeModel : public LayerModel{
  Q_OBJECT
  QML_NAMED_ELEMENT(LayerAttributeModel)
public:
  Q_DISABLE_COPY_MOVE(LayerAttributeModel)
  explicit LayerAttributeModel(QObject* parent = nullptr);
  ~LayerAttributeModel() override = default;
  void ProcessMessage(const tgfx::fbs::Layer* layer);
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  uint64_t GetSelectedAddress() const {
    return m_SelectedLayerAddress;
  }
  signals:
  void resetModel();
private:
  void ProcessLayerCommonAttribute(const tgfx::fbs::LayerCommonAttribute* commonAttribute,
    LayerItem* itemRoot);
  void ProcessImageLayerAttribute(const tgfx::fbs::ImageLayerAttribute* imageLayerAttribute,
    LayerItem* itemRoot);
  void ProcessShapeLayerAttribute(const tgfx::fbs::ShapeLayerAttribute* shapeLayerAttribute,
    LayerItem* itemRoot);
  void ProcessSolidLayerAttribute(const tgfx::fbs::SolidLayerAttribute* solidLayerAttribute,
    LayerItem* itemRoot);
  void ProcessTextLayerAttribute(const tgfx::fbs::TextLayerAttribute* textLayerAttribute,
    LayerItem* itemRoot);

  void ProcessLayerStyleAttribute(const tgfx::fbs::LayerStyle* layerStyle, LayerItem* parent);
  void ProcessLayerStyleCommonAttribute(const tgfx::fbs::LayerStyleCommonAttribute* commonAttribute, LayerItem* parent);

  void ProcessLayerFilterAttribute(const tgfx::fbs::LayerFilter* layerFilter, LayerItem* parent);
  void ProcessLayerFilterCommonAttribute(const tgfx::fbs::LayerfilterCommonAttribute* commonAttribute, LayerItem* parent);

  void ProcessImageAttribute(const tgfx::fbs::ImageAttribute* imageAttribute, LayerItem* parent);

  void ProcessShapeStyleAttribute(const tgfx::fbs::ShapeStyle* shapeStyle, LayerItem* parent);

  void ProcessGradientAttribute(const tgfx::fbs::GradientAttribute* gradientAttribute, LayerItem* parent);

  void ProcessShapeStyleCommonAttribute(const tgfx::fbs::ShapeStyleCommonAttribute* shapeStyleAttribute, LayerItem* parent);

  void ProcessColorAttribute(const tgfx::fbs::Color* color, LayerItem* parent);
private:
  uint64_t m_SelectedLayerAddress;
};



