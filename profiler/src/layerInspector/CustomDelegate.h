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

#include <QStyledItemDelegate>
#include <QPainter>

class CustomDelegate : public QStyledItemDelegate {
public:
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // 设置字体颜色和大小
    opt.palette.setColor(QPalette::Text, QColor("#000000"));  // 红色字体
    opt.font.setPointSize(17);

    // 调用基类绘制方法
    QStyledItemDelegate::paint(painter, opt, index);
  }

  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    // 获取默认大小
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    // 增加行间距
    size.setHeight(size.height() + m_Spacing);
    return size;
  }
private:
  int m_Spacing = 19;
};
