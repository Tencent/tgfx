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

#include "LayerAttributeView.h"
#include "CustomDelegate.h"
#include <QHeaderView>
#include <unordered_map>

using namespace tgfx::fbs;

template<typename T>
QStandardItem* SetSingleAttribute(__attribute__((unused))QStandardItem* parent,
  __attribute__((unused))const char* key, __attribute__((unused))T value) {
  assert(false);
}

template<>
QStandardItem* SetSingleAttribute<const char*>(QStandardItem* parent, const char* key,
  const char* value) {
  QStandardItem* keyItem = new QStandardItem(key);
  QStandardItem* valueItem = new QStandardItem(value);
  parent->appendRow({keyItem, valueItem});
  return keyItem;
}

template<>
QStandardItem* SetSingleAttribute<QString>(QStandardItem* parent, const char* key, QString value) {
  QStandardItem* keyItem = new QStandardItem(key);
  QStandardItem* valueItem = new QStandardItem(value);
  parent->appendRow({keyItem, valueItem});
  return keyItem;
}

template<>
QStandardItem* SetSingleAttribute<float>(QStandardItem* parent, const char* key,
  float value) {
  QStandardItem* keyItem = new QStandardItem(key);
  QStandardItem* valueItem = new QStandardItem();
  valueItem->setData(value, Qt::DisplayRole);
  valueItem->setText(QString::number(value, 'f', 2));
  parent->appendRow({keyItem, valueItem});
  return keyItem;
}

template<>
QStandardItem* SetSingleAttribute<bool>(QStandardItem* parent, const char* key,
  bool value) {
  QStandardItem* keyItem = new QStandardItem(key);
  QStandardItem* valueItem = new QStandardItem();
  valueItem->setData(value, Qt::DisplayRole);
  valueItem->setText(value ? "True" : "False");
  parent->appendRow({keyItem, valueItem});
  return keyItem;
}
template<>
QStandardItem* SetSingleAttribute<int>(QStandardItem* parent, const char* key, int value) {
  QStandardItem* keyItem = new QStandardItem(key);
  QStandardItem* valueItem = new QStandardItem();
  valueItem->setData(value, Qt::DisplayRole);
  valueItem->setText(QString::number(value));
  parent->appendRow({keyItem, valueItem});
  return keyItem;
}
template<>
QStandardItem* SetSingleAttribute<uint32_t>(QStandardItem* parent, const char* key, uint32_t value) {
  QStandardItem* keyItem = new QStandardItem(key);
  QStandardItem* valueItem = new QStandardItem();
  valueItem->setData(value, Qt::DisplayRole);
  valueItem->setText(QString::number(value));
  parent->appendRow({keyItem, valueItem});
  return keyItem;
}

static const char* LayerTypeToString(LayerType type) {
  static std::unordered_map<LayerType, const char*> m = {
    {LayerType_Layer, "Layer"}, {LayerType_Image, "Image"},
    {LayerType_Shape, "Shape"}, {LayerType_Gradient, "Gradient"},
    {LayerType_Gradient, "Gradient"}, {LayerType_Text, "Text"},
    {LayerType_Solid, "Solid"}
  };
  return m[type];
}

static const char* BlendModeToString(BlendMode blendMode) {
  static std::unordered_map<BlendMode, const char*> m = {
    {BlendMode_Clear, "Clear"}, {BlendMode_Src, "Src"}, {BlendMode_Dst, "Dst"},
    {BlendMode_SrcOver, "SrcOver"}, {BlendMode_DstOver, "DstOver"}, {BlendMode_SrcIn, "SrcIn"},
    {BlendMode_DstIn, "DstIn"}, {BlendMode_SrcOut, "SrcOut"}, {BlendMode_DstOut, "DstOut"},
    {BlendMode_SrcATop, "SrcATop"}, {BlendMode_DstATop, "DstATop"}, {BlendMode_Xor, "Xor"},
    {BlendMode_PlusLighter, "PlusLighter"}, {BlendMode_Modulate, "Modulate"}, {BlendMode_Screen, "Screen"},
    {BlendMode_Overlay, "Overlay"}, {BlendMode_Darken, "Darken"}, {BlendMode_Lighten, "Lighten"},
    {BlendMode_ColorDodge, "ColorDodge"}, {BlendMode_ColorBurn, "ColorBurn"}, {BlendMode_HardLight, "HardLight"},
    {BlendMode_SoftLight, "SoftLight"}, {BlendMode_Difference, "Difference"}, {BlendMode_Exclusion, "Exclusion"},
    {BlendMode_Multiply, "Multiply"}, {BlendMode_Hue, "Hue"}, {BlendMode_Saturation, "Saturation"}, {BlendMode_Color, "Color"},
    {BlendMode_Luminosity, "Luminosity"}, {BlendMode_PlusDarker, "PlusDarker"}
  };
  return m[blendMode];
}

static const char* LayerStyleTypeToString(LayerStyleType layerStyleType) {
  static std::unordered_map<LayerStyleType, const char*> m = {
    {LayerStyleType_BackgroundBlur, "BackgroundBlur"}, {LayerStyleType_DropShadow, "DropShadow"},
    {LayerStyleType_InnerShadow, "InnerShadow"}
  };
  return m[layerStyleType];
}

static const char* LayerStylePositionToString(LayerStylePosition position) {
  static std::unordered_map<LayerStylePosition, const char*> m = {
    {LayerStylePosition_Above, "Above"}, {LayerStylePosition_Below, "Blow"}
  };
  return m[position];
}

static const char* LayerStyleExtraSourceTypeToString(LayerStyleExtraSourceType sourceType) {
  static std::unordered_map<LayerStyleExtraSourceType, const char*> m = {
    {LayerStyleExtraSourceType_None, "None"}, {LayerStyleExtraSourceType_Contour, "Contour"},
    {LayerStyleExtraSourceType_Background, "Background"}
  };
  return m[sourceType];
}

static const char* TileModeToString(TileMode tileMode) {
  static std::unordered_map<TileMode, const char*> m = {
  {TileMode_Clamp, "Clamp"}, {TileMode_Repeat, "Repeat"}, {TileMode_Mirror, "Mirror"},
    {TileMode_Decal, "Decal"}
  };
  return m[tileMode];
}

static const char* LayerFilterTypeToString(LayerFilterType layerFilterType) {
  static std::unordered_map<LayerFilterType, const char*> m = {
    {LayerFilterType_LayerFilter, "LayerFilter"}, {LayerFilterType_BlendFilter, "BlendFilter"},
    {LayerFilterType_BlurFilter, "BlurFilter"}, {LayerFilterType_ColorMatrixFliter, "ColorMatrixFilter"},
    {LayerFilterType_DropShadowFilter, "DropShadowFilter"}, {LayerFilterType_InnerShadowFilter, "InnerShadowFilter"}
  };
  return m[layerFilterType];
}

static const char* FilterModeToString(FilterMode filterMode) {
  static std::unordered_map<FilterMode, const char*> m = {
    {FilterMode_Linear, "Linear"}, {FilterMode_Nearest, "Nearest"}
  };
  return m[filterMode];
}

static const char* MipmapModeToString(MipmapMode mipmapMode) {
  static std::unordered_map<MipmapMode, const char*> m = {
    {MipmapMode_None, "None"}, {MipmapMode_Linear, "Linear"},
    {MipmapMode_Nearest, "Nearest"}
  };
  return m[mipmapMode];
}

static const char* ImageTypeToString(ImageType imageType) {
  static std::unordered_map<ImageType, const char*> m = {
    {ImageType_Buffer, "Buffer"}, {ImageType_Codec, "Codec"}, {ImageType_Decoded, "Decoded"},
    {ImageType_Filter, "Filter"}, {ImageType_Generator, "Genderator"}, {ImageType_Mipmap, "Mipmap"},
    {ImageType_Orient, "Orient"}, {ImageType_Picture, "Picture"}, {ImageType_Rasterized, "Rasterized"},
    {ImageType_RGBAAA, "RGBAAA"}, {ImageType_Texture, "Texture"}, {ImageType_Subset, "Subset"}
  };
  return m[imageType];
}

static const char* PathFillTypeToString(PathFillType pathFillType) {
  static std::unordered_map<PathFillType, const char*> m = {
    {PathFillType_Winding, "Winding"}, {PathFillType_EvenOdd, "EvenOdd"},
    {PathFillType_InverseWinding, "InverseWinding"}, {PathFillType_InverseEvenOdd, "InvserEvenOdd"}
  };
  return m[pathFillType];
}

static const char* GradientTypeToString(GradientType gradientType) {
  static std::unordered_map<GradientType, const char*> m = {
    {GradientType_None, "None"}, {GradientType_Linear, "Linear"}, {GradientType_Radial, "Radial"},
    {GradientType_Conic, "Conic"}, {GradientType_Diamond, "Diamond"}
  };
  return m[gradientType];
}

static const char* TextAlignToString(TextAlign textAlign) {
  static std::unordered_map<TextAlign, const char*> m = {
    {TextAlign_Left, "Left"}, {TextAlign_Right, "Right"}, {TextAlign_Center, "Center"},
    {TextAlign_Justify, "Justify"}
  };
  return m[textAlign];
}


LayerAttributeView::LayerAttributeView(QWidget* parent)
  :QWidget(parent)
{
  setAttribute(Qt::WA_StyledBackground);
  setStyleSheet("background-color : white;");
  CreateWidget();
  CreateLayout();
  CreateConnect();
}

void LayerAttributeView::ProcessMessage(const tgfx::fbs::Layer* layer) {
  m_StandardItemModel->clear();
  m_StandardItemModel->setHorizontalHeaderLabels({"LayerAttribute", "Value"});
  QStandardItem* itemRoot = m_StandardItemModel->invisibleRootItem();

  switch(layer->layer_type()) {
    case tgfx::fbs::LayerType::LayerType_Layer: {
      auto commonAttribute = layer->layer_body_as_LayerCommonAttribute();
      ProcessLayerCommonAttribute(commonAttribute, itemRoot);
      break;
    }
    case tgfx::fbs::LayerType::LayerType_Image: {
      auto imageLayerAttribute = layer->layer_body_as_ImageLayerAttribute();
      ProcessImageLayerAttribute(imageLayerAttribute, itemRoot);
      break;
    }
    case tgfx::fbs::LayerType::LayerType_Shape: {
      auto shapeLayerAttribute = layer->layer_body_as_ShapeLayerAttribute();
      ProcessShapeLayerAttribute(shapeLayerAttribute, itemRoot);
      break;
    }
    case tgfx::fbs::LayerType::LayerType_Solid: {
      auto solidLayerAttribute = layer->layer_body_as_SolidLayerAttribute();
      ProcessSolidLayerAttribute(solidLayerAttribute, itemRoot);
      break;
    }
    case tgfx::fbs::LayerType::LayerType_Text: {
      auto textLayerAttribute = layer->layer_body_as_TextLayerAttribute();
      ProcessTextLayerAttribute(textLayerAttribute, itemRoot);
      break;
    }
    default:
      qDebug() << "Unknown layer type";
  }
  m_TreeView->viewport()->update();
  m_TreeView->expandAll();
}

void LayerAttributeView::CreateWidget() {
  m_TreeView = new QTreeView(this);
  m_StandardItemModel = new QStandardItemModel(this);
  m_StandardItemModel->setColumnCount(2);
  m_StandardItemModel->setHorizontalHeaderLabels({"LayerAttribute", "Value"});
  m_TreeView->setModel(m_StandardItemModel);
  m_TreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_TreeView->header()->setStretchLastSection(false);
  m_TreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
  m_TreeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_TreeView->header()->setSectionResizeMode(1,QHeaderView::Stretch);
  m_TreeView->header()->resizeSection(0, 7);
  m_TreeView->header()->resizeSection(1, 3);
  m_TreeView->header()->setMinimumSectionSize(50);

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
}

void LayerAttributeView::CreateLayout() {
  m_VLayout = new QVBoxLayout(this);
  m_VLayout->setContentsMargins(0, 0, 0, 0);
  m_VLayout->addWidget(m_TreeView);
}

void LayerAttributeView::CreateConnect() {

}

void LayerAttributeView::ProcessLayerCommonAttribute(
    const tgfx::fbs::LayerCommonAttribute* commonAttribute, QStandardItem* itemRoot) {
  m_SelectedLayerAddress = commonAttribute->address();
  QStandardItem* item = new QStandardItem("LayerCommonAttribute");
  QStandardItem* item1 = new QStandardItem();
  itemRoot->appendRow({item, item1});

  SetSingleAttribute(item, "Type", LayerTypeToString(commonAttribute->type()));
  SetSingleAttribute(item, "Name", commonAttribute->name()->c_str());
  SetSingleAttribute(item, "Alpha", commonAttribute->alpha());
  SetSingleAttribute(item, "BlendMode", BlendModeToString(commonAttribute->blend_mode()));

  QStandardItem* position = new QStandardItem("Position");
  QStandardItem* position1 = new QStandardItem();
  item->appendRow({position, position1});
  SetSingleAttribute(position, "X", commonAttribute->position()->x());
  SetSingleAttribute(position, "Y", commonAttribute->position()->y());

  SetSingleAttribute(item, "Visible", commonAttribute->visible());
  SetSingleAttribute(item, "Rasterize", commonAttribute->rasterize());
  SetSingleAttribute(item, "RasterizeScale", commonAttribute->rasterize_scale());
  SetSingleAttribute(item, "EdgeAntialiasing", commonAttribute->edge_antialiasing());
  SetSingleAttribute(item, "GroupOpacity", commonAttribute->grounp_opacity());

  QStandardItem* layerStylesItem = new QStandardItem("LayerStyles");
  uint32_t stylesNum = commonAttribute->layer_styles()->size();
  QStandardItem* layerStylesItem1 = new QStandardItem(stylesNum == 0 ? "Vector: Empty": "Vector: " + QString::number(stylesNum));
  item->appendRow({layerStylesItem, layerStylesItem1});
  for(auto layerStyle : *commonAttribute->layer_styles()) {
    QStandardItem* layerStyleItem = new QStandardItem("layerStyle");
    QStandardItem* layerStyleItem1 = new QStandardItem();
    layerStylesItem->appendRow({layerStyleItem, layerStyleItem1});
    ProcessLayerStyleAttribute(layerStyle, layerStyleItem);
  }

  QStandardItem* layerFiltersItem = new QStandardItem("LayerFilters");
  uint32_t filtersNum = commonAttribute->layer_filters()->size();
  QStandardItem* layerFiltersItem1 = new QStandardItem(filtersNum == 0 ? "Vector: Empty": "Vector: " + QString::number(filtersNum));
  item->appendRow({layerFiltersItem, layerFiltersItem1});
  for(auto layerFilter : *commonAttribute->layer_filters()) {
    QStandardItem* layerFilterItem = new QStandardItem("LayerFilter");
    QStandardItem* layerFilterItem1 = new QStandardItem();
    layerFiltersItem->appendRow({layerFilterItem, layerFilterItem1});
    ProcessLayerFilterAttribute(layerFilter, layerFilterItem);
  }

}

void LayerAttributeView::ProcessImageLayerAttribute(
    const tgfx::fbs::ImageLayerAttribute* imageLayerAttribute, QStandardItem* itemRoot) {
  QStandardItem* imageLayerItem = SetSingleAttribute(itemRoot, "ImageLayerAttribute", "");
  auto commonAttribute = imageLayerAttribute->common_attribute();
  ProcessLayerCommonAttribute(commonAttribute, imageLayerItem);
  SetSingleAttribute(imageLayerItem, "FilterMode",
    FilterModeToString(imageLayerAttribute->filter_mode()));
  SetSingleAttribute(imageLayerItem, "MipmapMode",
    MipmapModeToString(imageLayerAttribute->mipmap_mode()));
  ProcessImageAttribute(imageLayerAttribute->image(), imageLayerItem);
}

void LayerAttributeView::ProcessShapeLayerAttribute(
    const tgfx::fbs::ShapeLayerAttribute* shapeLayerAttribute, QStandardItem* itemRoot) {
  QStandardItem* shapeLayerItem = SetSingleAttribute(itemRoot, "ShapeLayerAttribute", "");
  auto commonAttrubte = shapeLayerAttribute->common_attribute();
  ProcessLayerCommonAttribute(commonAttrubte, shapeLayerItem);
  SetSingleAttribute(shapeLayerItem, "PathFillType",
    PathFillTypeToString(shapeLayerAttribute->path_fill_type()));
  SetSingleAttribute(shapeLayerItem, "IsLine", shapeLayerAttribute->path_is_line());
  SetSingleAttribute(shapeLayerItem, "IsRect", shapeLayerAttribute->path_is_rect());
  SetSingleAttribute(shapeLayerItem, "IsOval", shapeLayerAttribute->path_is_oval());
  SetSingleAttribute(shapeLayerItem, "IsEmpty", shapeLayerAttribute->path_is_empty());

  auto pathBoundsItem = SetSingleAttribute(shapeLayerItem, "PathBounds", "");
  SetSingleAttribute(pathBoundsItem, "Left", shapeLayerAttribute->path_bounds()->left());
  SetSingleAttribute(pathBoundsItem, "Right", shapeLayerAttribute->path_bounds()->right());
  SetSingleAttribute(pathBoundsItem, "Top", shapeLayerAttribute->path_bounds()->top());
  SetSingleAttribute(pathBoundsItem, "Bottom", shapeLayerAttribute->path_bounds()->bottom());

  SetSingleAttribute(shapeLayerItem, "PathPointCount", shapeLayerAttribute->path_point_count());
  SetSingleAttribute(shapeLayerItem, "PathVerbsCount", shapeLayerAttribute->path_verbs_count());

  uint32_t num = shapeLayerAttribute->shape_styles_attribute()->size();
  auto shapeStylesItem = SetSingleAttribute(shapeLayerItem, "ShapeStyles",
    num == 0 ? "Vector: Empty" : "Vector: " + QString::number(num));
  for(auto shapeStyle : *shapeLayerAttribute->shape_styles_attribute()) {
    auto shapeStyleItem = SetSingleAttribute(shapeStylesItem, "ShapeStyle", "");
    ProcessShapeStyleAttribute(shapeStyle, shapeStyleItem);
  }

}

void LayerAttributeView::ProcessSolidLayerAttribute(
    const tgfx::fbs::SolidLayerAttribute* solidLayerAttribute, QStandardItem* itemRoot) {
  QStandardItem* solidLayerItem = SetSingleAttribute(itemRoot, "SolidLayerAttribute", "");
  auto commonAttribute = solidLayerAttribute->common_attribute();
  ProcessLayerCommonAttribute(commonAttribute, solidLayerItem);
  SetSingleAttribute(solidLayerItem, "Width", solidLayerAttribute->width());
  SetSingleAttribute(solidLayerItem, "Height", solidLayerAttribute->height());
  SetSingleAttribute(solidLayerItem, "RadiusX", solidLayerAttribute->solid_radius_x());
  SetSingleAttribute(solidLayerItem, "RadiusY", solidLayerAttribute->solid_radius_y());

  QStandardItem* coloritem = SetSingleAttribute(solidLayerItem, "Color", "");
  SetSingleAttribute(coloritem, "Red", solidLayerAttribute->solid_color()->red());
  SetSingleAttribute(coloritem, "Green", solidLayerAttribute->solid_color()->green());
  SetSingleAttribute(coloritem, "Blue", solidLayerAttribute->solid_color()->blue());
  SetSingleAttribute(coloritem, "Alpha", solidLayerAttribute->solid_color()->alpha());
}

void LayerAttributeView::ProcessTextLayerAttribute(
    const tgfx::fbs::TextLayerAttribute* textLayerAttribute, QStandardItem* itemRoot) {
  QStandardItem* textLayerItem = SetSingleAttribute(itemRoot, "TextLayerAttribute", "");
  auto commonAttribute = textLayerAttribute->common_attribute();
  ProcessLayerCommonAttribute(commonAttribute, textLayerItem);
  SetSingleAttribute(textLayerItem, "String", textLayerAttribute->text_string()->c_str());

  QStandardItem* colorItem = SetSingleAttribute(textLayerItem, "Color", "");
  SetSingleAttribute(colorItem, "Red", textLayerAttribute->text_color()->red());
  SetSingleAttribute(colorItem, "Green", textLayerAttribute->text_color()->green());
  SetSingleAttribute(colorItem, "Blue", textLayerAttribute->text_color()->blue());
  SetSingleAttribute(colorItem, "Alpha", textLayerAttribute->text_color()->alpha());

  QStandardItem* fontItem = SetSingleAttribute(textLayerItem, "Font", "");
  SetSingleAttribute(fontItem, "HasColor", textLayerAttribute->text_font()->has_color());
  SetSingleAttribute(fontItem, "HasOutline", textLayerAttribute->text_font()->has_outlines());
  SetSingleAttribute(fontItem, "Size", textLayerAttribute->text_font()->size());
  SetSingleAttribute(fontItem, "IsFauxBold", textLayerAttribute->text_font()->is_faux_bold());
  SetSingleAttribute(fontItem, "IsFauxItalic", textLayerAttribute->text_font()->is_faux_ttalic());
  QStandardItem* typeFaceItem = SetSingleAttribute(fontItem, "TypeFace", "");
  SetSingleAttribute(typeFaceItem, "UniqueID", textLayerAttribute->text_font()->type_face()->unique_id());
  SetSingleAttribute(typeFaceItem, "FontFamily", textLayerAttribute->text_font()->type_face()->font_family()->c_str());
  SetSingleAttribute(typeFaceItem, "FontStyle", textLayerAttribute->text_font()->type_face()->font_style()->c_str());
  SetSingleAttribute(typeFaceItem, "GlyphsCount", textLayerAttribute->text_font()->type_face()->glyphs_count());
  SetSingleAttribute(typeFaceItem, "UnitsPerEm", textLayerAttribute->text_font()->type_face()->units_per_em());
  SetSingleAttribute(typeFaceItem, "HasColor", textLayerAttribute->text_font()->type_face()->has_color());
  SetSingleAttribute(typeFaceItem, "HasOutlines", textLayerAttribute->text_font()->type_face()->has_outlines());

  QStandardItem* fontMetricsItem = SetSingleAttribute(textLayerItem, "FontMetrics", "");
  SetSingleAttribute(fontMetricsItem, "Top", textLayerAttribute->font_metrics()->top());
  SetSingleAttribute(fontMetricsItem, "Ascent", textLayerAttribute->font_metrics()->ascent());
  SetSingleAttribute(fontMetricsItem, "Descent", textLayerAttribute->font_metrics()->descent());
  SetSingleAttribute(fontMetricsItem, "Bottom", textLayerAttribute->font_metrics()->bottom());
  SetSingleAttribute(fontMetricsItem, "Leading", textLayerAttribute->font_metrics()->leading());
  SetSingleAttribute(fontMetricsItem, "XMin", textLayerAttribute->font_metrics()->x_min());
  SetSingleAttribute(fontMetricsItem, "XMax", textLayerAttribute->font_metrics()->x_max());
  SetSingleAttribute(fontMetricsItem, "XHeight", textLayerAttribute->font_metrics()->x_height());
  SetSingleAttribute(fontMetricsItem, "CapHeight", textLayerAttribute->font_metrics()->cap_height());
  SetSingleAttribute(fontMetricsItem, "UnderlineThickness", textLayerAttribute->font_metrics()->underline_thickness());
  SetSingleAttribute(fontMetricsItem, "UnderlinePosition", textLayerAttribute->font_metrics()->underline_position());

  SetSingleAttribute(textLayerItem, "TextWidth", textLayerAttribute->text_width());
  SetSingleAttribute(textLayerItem, "TextHeight", textLayerAttribute->text_height());
  SetSingleAttribute(textLayerItem, "TextAlign", TextAlignToString(textLayerAttribute->text_align()));
  SetSingleAttribute(textLayerItem, "TextAutoWrap", textLayerAttribute->text_auto_wrap());

}

void LayerAttributeView::ProcessLayerStyleAttribute(const tgfx::fbs::LayerStyle* layerStyle,
    QStandardItem* parent) {
  switch(layerStyle->style_type()) {
    case LayerStyleType_BackgroundBlur: {
      auto backGround = layerStyle->style_body_as_BackGroundBlurStyleAttribute();
      auto commonAttribute = backGround->common_attribute();
      ProcessLayerStyleCommonAttribute(commonAttribute, parent);
      SetSingleAttribute(parent, "BlurrinessX", backGround->blurriness_x());
      SetSingleAttribute(parent, "BlurrinessY", backGround->blurriness_y());
      SetSingleAttribute(parent, "TileMode", TileModeToString(backGround->tile_mode()));
      break;
    }
    case LayerStyleType_DropShadow: {
      auto dropShadow = layerStyle->style_body_as_DropShadowStyleAttribute();
      auto commonAttribute = dropShadow->common_attribute();
      ProcessLayerStyleCommonAttribute(commonAttribute, parent);
      SetSingleAttribute(parent, "OffsetX", dropShadow->offset_x());
      SetSingleAttribute(parent, "OffsetY", dropShadow->offset_y());
      SetSingleAttribute(parent, "BlurrinessX", dropShadow->blurriness_x());
      SetSingleAttribute(parent, "BlurrinessY", dropShadow->blurriness_y());
      QStandardItem* colorItem = new QStandardItem("Color");

      parent->appendRow(colorItem);
      SetSingleAttribute(colorItem, "Red", dropShadow->color()->red());
      SetSingleAttribute(colorItem, "Green", dropShadow->color()->green());
      SetSingleAttribute(colorItem, "Blue", dropShadow->color()->blue());
      SetSingleAttribute(colorItem, "Alpha", dropShadow->color()->alpha());

      SetSingleAttribute(parent, "ShowBehindLayer", dropShadow->show_behind_layer());
      break;
    }

    case LayerStyleType_InnerShadow: {
      auto innerShadow = layerStyle->style_body_as_InnerShadowStyleAttribute();
      auto commonAttribute = innerShadow->common_attribute();
      ProcessLayerStyleCommonAttribute(commonAttribute, parent);
      SetSingleAttribute(parent, "OffsetX", innerShadow->offset_x());
      SetSingleAttribute(parent, "OffsetY", innerShadow->offset_y());
      SetSingleAttribute(parent, "BlurrinessX", innerShadow->blurriness_x());
      SetSingleAttribute(parent, "BlurrinessY", innerShadow->blurriness_y());

      QStandardItem* colorItem = new QStandardItem("Color");

      parent->appendRow(colorItem);
      SetSingleAttribute(colorItem, "Red", innerShadow->color()->red());
      SetSingleAttribute(colorItem, "Green", innerShadow->color()->green());
      SetSingleAttribute(colorItem, "Blue", innerShadow->color()->blue());
      SetSingleAttribute(colorItem, "Alpha", innerShadow->color()->alpha());
      break;
    }
  }
}

void LayerAttributeView::ProcessLayerStyleCommonAttribute(
    const tgfx::fbs::LayerStyleCommonAttribute* commonAttribute, QStandardItem* parent) {
  QStandardItem* CommonAttributeItem = new QStandardItem("LayerStyleCommonAttribute");
  QStandardItem* CommonAttributeItem1 = new QStandardItem();
  parent->appendRow({CommonAttributeItem, CommonAttributeItem1});

  SetSingleAttribute(CommonAttributeItem, "Type",
    LayerStyleTypeToString(commonAttribute->type()));
  SetSingleAttribute(CommonAttributeItem, "BlendMode",
    BlendModeToString(commonAttribute->blend_mode()));
  SetSingleAttribute(CommonAttributeItem, "Position",
    LayerStylePositionToString(commonAttribute->position()));
  SetSingleAttribute(CommonAttributeItem, "SourceType",
    LayerStyleExtraSourceTypeToString(commonAttribute->source_type()));
}

void LayerAttributeView::ProcessLayerFilterAttribute(const tgfx::fbs::LayerFilter* layerFilter,
    QStandardItem* parent) {
  switch(layerFilter->filter_type()) {
    case LayerFilterType_BlendFilter: {
      auto blendFilter = layerFilter->filter_body_as_BlendFilterAttribute();
      auto commonAttribute = blendFilter->common_attribute();
      ProcessLayerFilterCommonAttribute(commonAttribute, parent);

      QStandardItem* colorItem = new QStandardItem("Color");
      parent->appendRow(colorItem);
      SetSingleAttribute(colorItem, "Red", blendFilter->color()->red());
      SetSingleAttribute(colorItem, "Green", blendFilter->color()->green());
      SetSingleAttribute(colorItem, "Blue", blendFilter->color()->blue());
      SetSingleAttribute(colorItem, "Alpha", blendFilter->color()->alpha());

      SetSingleAttribute(parent, "BlendMode", BlendModeToString(blendFilter->blend_mode()));
      break;
    }
    case LayerFilterType_BlurFilter: {
      auto blurFilter = layerFilter->filter_body_as_BlurFilterAttribute();
      auto commonAttribute = blurFilter->common_attribute();
      ProcessLayerFilterCommonAttribute(commonAttribute, parent);

      SetSingleAttribute(parent, "BlurrinessX", blurFilter->blurriness_x());
      SetSingleAttribute(parent, "BlurrinessY", blurFilter->blurriness_y());
      SetSingleAttribute(parent, "TileMode", TileModeToString(blurFilter->tile_mode()));
      break;
    }
    case LayerFilterType_ColorMatrixFliter: {
      auto colorMatrixFilter = layerFilter->filter_body_as_ColorFilterAttribute();
      auto commonAttribute = colorMatrixFilter->common_attribute();
      ProcessLayerFilterCommonAttribute(commonAttribute, parent);

      QStandardItem* matrix = new QStandardItem("Matrix");
      parent->appendRow(matrix);
      auto elements = colorMatrixFilter->matrix()->elements();
      for(int i = 0; i < elements->size(); i++) {
        SetSingleAttribute(matrix, QString::number(i).toUtf8(), elements->Get((uint32_t)i));
      }
      break;
    }
    case LayerFilterType_DropShadowFilter: {
      auto dropShadowFilter = layerFilter->filter_body_as_DropShadowFilterAttribute();
      auto commonAttribute = dropShadowFilter->common_attribute();
      ProcessLayerFilterCommonAttribute(commonAttribute, parent);

      SetSingleAttribute(parent, "OffsetX", dropShadowFilter->offset_x());
      SetSingleAttribute(parent, "OffsetY", dropShadowFilter->offset_y());
      SetSingleAttribute(parent, "BlurrinessX", dropShadowFilter->blurriness_x());
      SetSingleAttribute(parent, "BlurrinessY", dropShadowFilter->blurriness_y());

      QStandardItem* colorItem = new QStandardItem("Color");
      parent->appendRow(colorItem);
      SetSingleAttribute(colorItem, "Red", dropShadowFilter->color()->red());
      SetSingleAttribute(colorItem, "Green", dropShadowFilter->color()->green());
      SetSingleAttribute(colorItem, "Blue", dropShadowFilter->color()->blue());
      SetSingleAttribute(colorItem, "Alpha", dropShadowFilter->color()->alpha());

      SetSingleAttribute(parent, "DropShadowOnly", dropShadowFilter->drop_shadow_only());

      break;
    }
    case LayerFilterType_InnerShadowFilter: {
      auto innerShadowFilter = layerFilter->filter_body_as_InnerShadowFilterAttribute();
      auto commonAttribute = innerShadowFilter->common_attribute();
      ProcessLayerFilterCommonAttribute(commonAttribute, parent);

      SetSingleAttribute(parent, "OffsetX", innerShadowFilter->offset_x());
      SetSingleAttribute(parent, "OffsetY", innerShadowFilter->offset_y());
      SetSingleAttribute(parent, "BlurrinessX", innerShadowFilter->blurriness_x());
      SetSingleAttribute(parent, "BlurrinessY", innerShadowFilter->blurriness_y());

      QStandardItem* colorItem = new QStandardItem("Color");
      parent->appendRow(colorItem);
      SetSingleAttribute(colorItem, "Red", innerShadowFilter->color()->red());
      SetSingleAttribute(colorItem, "Green", innerShadowFilter->color()->green());
      SetSingleAttribute(colorItem, "Blue", innerShadowFilter->color()->blue());
      SetSingleAttribute(colorItem, "Alpha", innerShadowFilter->color()->alpha());

      SetSingleAttribute(parent, "InnerShadowOnly", innerShadowFilter->inner_shadow_only());
      break;
    }
    default:
      qDebug() << "Unknown filter type";
  }
}

void LayerAttributeView::ProcessLayerFilterCommonAttribute(
    const tgfx::fbs::LayerfilterCommonAttribute* commonAttribute, QStandardItem* parent) {
  QStandardItem* CommonAttributeItem = new QStandardItem("LayerFilterCommonAttribute");
  QStandardItem* CommonAttributeItem1 = new QStandardItem();
  parent->appendRow({CommonAttributeItem, CommonAttributeItem1});
  SetSingleAttribute(CommonAttributeItem, "Type",
    LayerFilterTypeToString(commonAttribute->type()));

}

void LayerAttributeView::ProcessImageAttribute(const tgfx::fbs::ImageAttribute* imageAttribute,
    QStandardItem* parent) {
  auto imageItem = SetSingleAttribute(parent, "Image", "");
  SetSingleAttribute(imageItem, "ImageType",
    ImageTypeToString(imageAttribute->image_type()));
  SetSingleAttribute(imageItem, "ImageWidth", imageAttribute->image_width());
  SetSingleAttribute(imageItem, "ImageHeight", imageAttribute->image_height());
  SetSingleAttribute(imageItem, "ImageAlphaOnly", imageAttribute->image_alpha_only());
  SetSingleAttribute(imageItem, "ImageMipmap", imageAttribute->image_mipmap());
  SetSingleAttribute(imageItem, "ImageFullyDecode", imageAttribute->image_fully_decode());
  SetSingleAttribute(imageItem, "ImageTextureBacked", imageAttribute->image_texture_backed());
}

void LayerAttributeView::ProcessShapeStyleAttribute(const tgfx::fbs::ShapeStyle* shapeStyle,
    QStandardItem* parent) {
  switch (shapeStyle->shapestyle_type()) {
    case ShapeStyleType_LinearGradient: {
      auto linearGradient = shapeStyle->shapestyle_body_as_LinearGradientAttribute();
      auto gradient = linearGradient->gradient_attribute();
      ProcessGradientAttribute(gradient, parent);
      auto startPointItem = SetSingleAttribute(parent, "StartPoint", "");
      SetSingleAttribute(startPointItem, "X", linearGradient->start_point()->x());
      SetSingleAttribute(startPointItem, "Y", linearGradient->start_point()->y());
      auto endPointItem = SetSingleAttribute(parent, "EndPoint", "");
      SetSingleAttribute(endPointItem, "X", linearGradient->end_point()->x());
      SetSingleAttribute(endPointItem, "Y", linearGradient->end_point()->y());
      break;
    }
    case ShapeStyleType_RadiusGradient: {
      auto radiusGradient = shapeStyle->shapestyle_body_as_RadialGradientAttribute();
      auto gradient = radiusGradient->gradient_attribute();
      ProcessGradientAttribute(gradient, parent);

      auto centerItem = SetSingleAttribute(parent, "Center", "");
      SetSingleAttribute(centerItem, "X", radiusGradient->center()->x());
      SetSingleAttribute(centerItem, "Y", radiusGradient->center()->y());

      SetSingleAttribute(parent, "Radius", radiusGradient->radius());
      break;
    }
    case ShapeStyleType_ConicGradient: {
      auto conicGradient = shapeStyle->shapestyle_body_as_ConicGradientAttribute();
      auto gradient = conicGradient->gradient_attribute();
      ProcessGradientAttribute(gradient, parent);

      auto centerItem = SetSingleAttribute(parent, "Center", "");
      SetSingleAttribute(centerItem, "X", conicGradient->center()->x());
      SetSingleAttribute(centerItem, "Y", conicGradient->center()->y());

      SetSingleAttribute(parent, "StartAngle", conicGradient->start_angle());
      SetSingleAttribute(parent, "EndAngle", conicGradient->end_angle());
      break;
    }
    case ShapeStyleType_DiamondGradient: {
      auto diamondGradient = shapeStyle->shapestyle_body_as_DiamondGradientAttribute();
      auto gradient = diamondGradient->gradient_attribute();
      ProcessGradientAttribute(gradient, parent);

      auto centerItem = SetSingleAttribute(parent, "Center", "");
      SetSingleAttribute(centerItem, "X", diamondGradient->center()->x());
      SetSingleAttribute(centerItem, "Y", diamondGradient->center()->y());

      SetSingleAttribute(parent, "HalfDiagonal", diamondGradient->half_diagonal());
      break;
    }
    case ShapeStyleType_ImagePattern: {
      auto imagePattern = shapeStyle->shapestyle_body_as_ImagePatternAttribute();
      auto shapeStyleCommon = imagePattern->common_attribute();
      ProcessShapeStyleCommonAttribute(shapeStyleCommon, parent);

      SetSingleAttribute(parent, "TileModeX", TileModeToString(imagePattern->tilemode_x()));
      SetSingleAttribute(parent, "TileModeY", TileModeToString(imagePattern->tilemode_y()));
      SetSingleAttribute(parent, "FilterMode", FilterModeToString(imagePattern->filtermode()));
      SetSingleAttribute(parent, "MipmapMode", MipmapModeToString(imagePattern->mipmapmode()));
      ProcessImageAttribute(imagePattern->image(), parent);
      break;
    }
  }
}

void LayerAttributeView::ProcessGradientAttribute(
    const tgfx::fbs::GradientAttribute* gradientAttribute, QStandardItem* parent) {
  auto gradientItem = SetSingleAttribute(parent, "Gradient", "");
  auto shapeStyleAttribute = gradientAttribute->common_attribute();
  ProcessShapeStyleCommonAttribute(shapeStyleAttribute, gradientItem);
  SetSingleAttribute(gradientItem, "Type", GradientTypeToString(gradientAttribute->type()));

  uint32_t colorNum = gradientAttribute->colors()->size();
  auto colorsItem = SetSingleAttribute(gradientItem, "Colors",
    colorNum == 0 ? "Vector: Empty" : "Vector: " + QString::number(colorNum));
  for(auto color : *gradientAttribute->colors()) {
    ProcessColorAttribute(color, colorsItem);
  }

  uint32_t positionNum = gradientAttribute->positions()->size();
  auto positionsItem = SetSingleAttribute(gradientItem, "Positions",
    positionNum == 0 ? "Vector: Empty" : "Vector: " + QString::number(positionNum));
  for(auto position : *gradientAttribute->positions()) {
    SetSingleAttribute(positionsItem, "Position", position);
  }
}

void LayerAttributeView::ProcessShapeStyleCommonAttribute(
    const tgfx::fbs::ShapeStyleCommonAttribute* shapeStyleAttribute, QStandardItem* parent) {
  auto shapeStyleItem = SetSingleAttribute(parent, "ShapeStyleAttribute", "");
  SetSingleAttribute(shapeStyleItem, "Alpha", shapeStyleAttribute->shape_style_alpha());
  SetSingleAttribute(shapeStyleItem, "BlendMode",
    BlendModeToString(shapeStyleAttribute->blend_mode()));
}

void LayerAttributeView::
ProcessColorAttribute(const tgfx::fbs::Color* color, QStandardItem* parent) {
  auto colorItem = SetSingleAttribute(parent, "Color", "");
  SetSingleAttribute(colorItem, "Red", color->red());
  SetSingleAttribute(colorItem, "Green", color->green());
  SetSingleAttribute(colorItem, "Blue", color->blue());
  SetSingleAttribute(colorItem, "Alpha", color->alpha());
}

