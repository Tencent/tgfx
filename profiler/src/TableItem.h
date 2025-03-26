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

#include "StatisticModel.h"

class TableItem : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(StatisticsModel* model READ getModel WRITE setModel NOTIFY modelChanged)
  Q_PROPERTY(int rowHeight READ getRowHeight WRITE setRowHeight NOTIFY rowHeightChanged)
  Q_PROPERTY(int scrollPosition READ getScrollPosition WRITE setScrollPosition NOTIFY
                 scrollPositionChanged)
  Q_PROPERTY(int nameColumnWidth READ getNameColumnWidth WRITE setNameColumnWidth NOTIFY
                 nameColumnWidthChanged)
  Q_PROPERTY(int locationColumnWidth READ getLocationColumnWidth WRITE setLocationColumnWidth NOTIFY
                 locationColumnWidthChanged)
  Q_PROPERTY(int totalTimeWidth READ getTotalTimeWidth WRITE setTotalTimeWidth NOTIFY
                 totalTimeWidthChanged)
  Q_PROPERTY(int countWidth READ getCountWidth WRITE setCountWidth NOTIFY countWidthChanged)
  Q_PROPERTY(int mtpcWidth READ getMtpcWidth WRITE setMtpcWidth NOTIFY mtpcWidthChanged)
  Q_PROPERTY(int threadsWidth READ getThreadsWidth WRITE setThreadsWidth NOTIFY threadsWidthChanged)
  Q_PROPERTY(int sortColumn READ getSortColumn WRITE setSortColumn NOTIFY sortColumnChanged)
  Q_PROPERTY(int sortOrder READ getSortOrder WRITE setSortOrder NOTIFY sortOrderChanged)

 public:
  explicit TableItem(QQuickItem* parent = nullptr);
  ~TableItem() override;

  StatisticsModel* getModel() const {
    return tModel;
  }
  int getRowHeight() const {
    return rowHeight;
  }
  int getScrollPosition() const {
    return scrollPosition;
  }
  int getNameColumnWidth() const {
    return nameColumnWidth;
  }
  int getLocationColumnWidth() const {
    return locationColumnWidth;
  }
  int getTotalTimeWidth() const {
    return totalTimeWidth;
  }
  int getCountWidth() const {
    return countWidth;
  }
  int getMtpcWidth() const {
    return mtpcWidth;
  }
  int getThreadsWidth() const {
    return threadsWidth;
  }
  int getSortColumn() const {
    return sortColumn;
  }
  int getSortOrder() const {
    return sortOrder;
  }

  void setModel(StatisticsModel* model);
  void setRowHeight(int height);
  void setScrollPosition(int position);
  void setNameColumnWidth(int width);
  void setLocationColumnWidth(int width);
  void setTotalTimeWidth(int width);
  void setCountWidth(int width);
  void setMtpcWidth(int width);
  void setThreadsWidth(int width);
  void setSortColumn(int column);
  void setSortOrder(int order);

  Q_INVOKABLE void handleMouseDoubleClick(int x, int y);

 Q_SIGNALS:
  void modelChanged();
  void rowHeightChanged();
  void scrollPositionChanged();
  void nameColumnWidthChanged();
  void locationColumnWidthChanged();
  void totalTimeWidthChanged();
  void countWidthChanged();
  void mtpcWidthChanged();
  void threadsWidthChanged();
  void rowCountChanged();
  void visibleRowCountChanged(int count);
  void sortColumnChanged();
  void sortOrderChanged();

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
  void createAppHost();
  void draw();
  void drawTable(tgfx::Canvas* canvas);
  void drawRow(tgfx::Canvas* canvas, int rowIndex, float y);
  void drawCell(tgfx::Canvas* canvas, const QString& text, float x, float y, float width,
                const QColor& textColor, bool contrast = false,
                Qt::Alignment alignment = Qt::AlignLeft);
  tgfx::Rect getTextBounds(const QString& text) const;
  QString elideText(const QString& text, float maxWidth, Qt::TextElideMode elideMode) const;

 private:
  StatisticsModel* tModel = nullptr;
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow;
  std::unique_ptr<AppHost> appHost;

  int rowHeight = 36;
  int scrollPosition = 0;
  int nameColumnWidth = 200;
  int locationColumnWidth = 200;
  int totalTimeWidth = 160;
  int countWidth = 80;
  int mtpcWidth = 130;
  int threadsWidth = 70;

  bool geometryChanged = true;
  int visibleRowCount = 0;
  int firstVisibleCount = 0;
  int hoveredRow = -1;
  int selectedRow = -1;

  int sortColumn = 2;
  int sortOrder = Qt::DescendingOrder;
};