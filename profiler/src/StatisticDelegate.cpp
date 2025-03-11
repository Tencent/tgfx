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

#include "StatisticDelegate.h"
#include <QMenu>
#include <QPainterPath>
#include "View.h"

StatisticsDelegate::StatisticsDelegate(StatisticsModel* model, View* v, QObject* parent)
    : QStyledItemDelegate(parent), hoverColor(51, 153, 255, 90), textColor(255, 255, 255, 230),
      iconSize(16, 16), view(v), model(model) {
}

StatisticsDelegate::~StatisticsDelegate() = default;

void StatisticsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);

  if (index.column() == StatisticsModel::NameColumn) {
    painter->save();

    if (opt.state & QStyle::State_Selected) {
      painter->fillRect(opt.rect, opt.palette.highlight());
    } else if (opt.state & QStyle::State_MouseOver) {
      painter->fillRect(opt.rect, hoverColor);
    } else {
      painter->fillRect(opt.rect, opt.palette.base());
    }

    constexpr int padding = 0;
    QRect contentRect = opt.rect.adjusted(padding, 0, -padding, 0);
    QRect iconRect = contentRect;

    iconRect.setSize(iconSize);
    iconRect.moveCenter(QPoint(iconRect.left() + iconSize.width() / 2, contentRect.center().y()));

    drawStatusIcon(painter, iconRect, index);

    QRect textRect = contentRect.adjusted(iconSize.width() + padding, 0, 0, 0);
    QFont nameFont = opt.font;
    painter->setFont(nameFont);

    if (opt.state & QStyle::State_Selected) {
      painter->setPen(opt.palette.highlightedText().color());
    } else {
      painter->setPen(textColor);
    }

    QString name = index.data(Qt::DisplayRole).toString();
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, name);

    painter->restore();

  } else if (index.column() == StatisticsModel::LocationColumn) {

    painter->save();

    if (opt.state & QStyle::State_Selected) {
      painter->fillRect(opt.rect, opt.palette.highlight());
      painter->setPen(opt.palette.highlightedText().color());
    } else {
      painter->fillRect(opt.rect, opt.palette.base());
      painter->setPen(QColor(128, 128, 128));
    }

    painter->drawText(opt.rect.adjusted(3, 0, -3, 0), static_cast<int>(opt.displayAlignment),
                      index.data(Qt::DisplayRole).toString());
    painter->restore();
  } else if (index.column() == StatisticsModel::TotalTimeColumn) {
    painter->save();

    QString timeStr = index.data(Qt::DisplayRole).toString();
    int64_t timeRange = 0;
    if (view->m_statRange.active) {
      timeRange = view->m_statRange.max - view->m_statRange.min;
      if (timeRange == 0) {
        timeRange = 1;
      }
    } else {
      timeRange = model->getWorker().GetLastTime() - model->getWorker().GetFirstTime();
    }

    const auto& entry = model->getSrcData()[static_cast<size_t>(index.row())];
    const auto rawTime = entry.total;

    double percentage = (timeRange > 0) ? (rawTime * 100.0 / timeRange) : 0.0;
    QString percentStr = QString("(%1%)").arg(percentage, 0, 'f', 2);

    QColor textColor;
    if (opt.state & QStyle::State_Selected) {
      painter->fillRect(opt.rect, opt.palette.highlight());
      textColor = opt.palette.highlightedText().color();
    } else {
      painter->fillRect(opt.rect, opt.palette.base());
      textColor = opt.palette.text().color();
    }

    painter->setPen(textColor);
    QRect timeRect = opt.rect;
    timeRect.adjust(3, 0, -3, 0);
    painter->drawText(timeRect, Qt::AlignVCenter | Qt::AlignLeft, timeStr);

    QFontMetrics fm(opt.font);
    int timeWidth = fm.horizontalAdvance(timeStr);
    QRect percentRect = timeRect;
    percentRect.setLeft(timeRect.left() + timeWidth);

    QColor percentColor = textColor;
    percentColor.setAlpha(180);
    painter->setPen(percentColor);
    painter->drawText(percentRect, Qt::AlignVCenter | Qt::AlignLeft, percentStr);

    painter->restore();
  } else {
    QStyledItemDelegate::paint(painter, option, index);
  }
}

QSize StatisticsDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
  QSize size = QStyledItemDelegate::sizeHint(option, index);
  size.setHeight(qMax(size.height(), iconSize.height() + 50));
  return size;
}

void StatisticsDelegate::drawStatusIcon(QPainter* painter, const QRect& rect,
                                        const QModelIndex& index) const {

  auto& srcloc = model->getSrcLocFromIndex(index);
  QColor color = model->getStrLocColor(srcloc, 0);

  QPainterPath path;
  path.addEllipse(rect.adjusted(2, 2, -2, -2));
  painter->fillPath(path, color);
  painter->setPen(QPen(QColor(200, 200, 200, 80), 1));
  painter->drawPath(path);
}
