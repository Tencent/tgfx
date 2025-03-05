#pragma once

#include "StatisticView.h"
#include "StatisticModel.h"
#include <QStyledItemDelegate>


class StatisticsDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  explicit StatisticsDelegate(StatisticsModel* model,View* v, QObject* parent = nullptr);
  ~StatisticsDelegate() override;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  void drawStatusIcon(QPainter* painter, const QRect& rect, const QModelIndex& index) const;

private:
  QColor hoverColor;
  QColor textColor;
  QSize iconSize;
  View* view;
  StatisticsModel* model = nullptr;

};
