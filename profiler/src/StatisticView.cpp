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

#include "StatisticView.h"
#include <QButtonGroup>
#include <QRadioButton>
#include <QTableView>
#include <QMenu>
#include "MainView.h"
#include "StatisticDelegate.h"

/////*statistics view*/////
StatisticsView::StatisticsView(tracy::Worker& workerRef,ViewData& viewDataRef ,View* view,FramesView* framesView,SourceView* srcView, QWidget* parent) : QWidget(parent),
  worker(workerRef),
  viewData(viewDataRef),
  view(view),
  framesView(framesView),
  tableView(new QTableView(this)),
  srcView(srcView)
  {
  model = new StatisticsModel(worker,viewData, view, this);

  int totalWidth = 500 + 400 + 120 + 80 + 100 + 30;
  int initHeight = 1500;
  resize(totalWidth, initHeight);
  setMinimumSize(totalWidth, 400);
  setupUI();
  setupConnections();

  if(layout()) {
    layout()->setSizeConstraint(QLayout::SetMinimumSize);
  }
  updateZoneCountLabels();

}

void StatisticsView::setupUI() {

  auto mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  //tool bar
  auto toolBar = new QWidget(this);
  toolBar->setStyleSheet("background-color: #2D2D2D;");
  auto toolBarLayout = new QHBoxLayout(toolBar);
  toolBarLayout->setContentsMargins(8, 8, 8, 8);

  //mode btn
  auto modeGroup = new QButtonGroup(this);

  auto insBtn = new QRadioButton(tr("instrumentation"),this);
  insBtn->setStyleSheet("QRadioButton {color:white;}");
  insBtn->setChecked(true);

  // auto samBtn = new QRadioButton(tr("Sampling"), this);
  // samBtn->setStyleSheet("QRadioButton {color:white;}");
  //
   auto gpuBtn = new QRadioButton(tr("GPU"), this);
   gpuBtn->setStyleSheet("QRadioButton {color:white;}");

  insBtn->setChecked(true);
  modeGroup->addButton(insBtn, 0);
  // modeGroup->addButton(samBtn, 1);
   modeGroup->addButton(gpuBtn, 1);

  toolBarLayout->addWidget(insBtn);
  // toolBarLayout->addWidget(samBtn);
   toolBarLayout->addWidget(gpuBtn);

  //seperator
  auto sep1 = new QLabel("|", this);
  sep1->setStyleSheet("color: #666666;");
  toolBarLayout->addWidget(sep1);

  //zone and visible
  auto totalZonesLabel = new QLabel(tr("Total zone count:"),this);
  totalZonesLabel->setStyleSheet("color: white;");
  mtotalZonesLabel = new QLabel(QStringLiteral("0"), this);
  mtotalZonesLabel->setStyleSheet("color: white;");

  auto visZonesLabel = new QLabel(tr("Visible zones:"), this);
  visZonesLabel->setStyleSheet("color: white;");
  mvisibleZonesLabel = new QLabel(QStringLiteral("0"), this);
  mvisibleZonesLabel->setStyleSheet("color: white;");

  toolBarLayout->addWidget(totalZonesLabel);
  toolBarLayout->addWidget(mtotalZonesLabel);
  toolBarLayout->addSpacing(10);
  toolBarLayout->addWidget(visZonesLabel);
  toolBarLayout->addWidget(mvisibleZonesLabel);

  //seperator
  auto sep2 = new QLabel("|", this);
  sep2->setStyleSheet("color: #666666;");
  toolBarLayout->addWidget(sep2);

  //timing combobox
  auto timingLabel = new QLabel(tr("Timing:"), this);
  timingLabel->setStyleSheet("color: white;");
  accumulationModeCombo = new QComboBox(this);
  accumulationModeCombo->setStyleSheet(
    "QComboBox {color: white; background: #404040; border: 1px solid #555555; padding: 2px;}"
    "QComboBox::drop-down {border none;}");

  accumulationModeCombo->addItem(tr("Self only"),
    QVariant::fromValue(StatisticsModel::AccumulationMode::SelfOnly));
  accumulationModeCombo->addItem(tr("With children"),
    QVariant::fromValue(StatisticsModel::AccumulationMode::AllChildren));
  accumulationModeCombo->addItem(tr("Non-reentrant"),
    QVariant::fromValue(StatisticsModel::AccumulationMode::NonReentrantChildren));

  toolBarLayout->addWidget(timingLabel);
  toolBarLayout->addWidget(accumulationModeCombo);
  toolBarLayout->addStretch();

  //filter text
  auto filterWidget = new QWidget(this);
  filterWidget->setStyleSheet("background-color: #2D2D2D;");
  auto filterLayout = new QHBoxLayout(filterWidget);
  filterLayout->setContentsMargins(8, 4, 8, 4);

  auto nameLabel = new QLabel(tr("Name"), this);
  nameLabel->setStyleSheet("color: white;");
  filterEdit = new QLineEdit(this);
  filterEdit->setStyleSheet("QLineEdit {background: #404040; color: white; border: 1px solid #555555; padding: 4px;}");
  filterEdit->setPlaceholderText(tr("Enter filter Text..."));

  clearFilterButton = new QPushButton(tr("Clear"), this);
  clearFilterButton->setStyleSheet("QPushButton {background: #404040; color: white; border: 1px solid #555555; padding: 4px 8px;}"
    "QPushButton: hover {background: #505050;}");

  filterLayout->addWidget(nameLabel);
  filterLayout->addWidget(filterEdit);
  filterLayout->addWidget(clearFilterButton);
  filterLayout->addStretch();

  auto sep3 = new QLabel("|",this);
  sep3->setStyleSheet("color:#666666;");
  toolBarLayout->addWidget(sep3);

  limitRangeBtn = new QPushButton(tr("Limit Range"), this);
  limitRangeBtn->setStyleSheet(
    "QPushButton {background: #404040; color: white; border: 1px solid #555555; padding: 4px 8px;}"
    "QPushButton: hover{background: #505050;}"
    "QPushButton: checked{background: #8B3A62; color: white;}"
    );
  limitRangeBtn->setCheckable(true);
  toolBarLayout->addWidget(limitRangeBtn);

  setupTableView();
  mainLayout->addWidget(toolBar);
  mainLayout->addWidget(filterWidget);
  mainLayout->addWidget(tableView);
}

void StatisticsView::setupConnections() {
  auto modeGroup = findChild<QButtonGroup*>();
  connect(modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
    if (model) {
      model->setStatisticsMode(static_cast<StatisticsModel::StatMode>(id));
      updateZoneCountLabels();
    }
  });

  connect(accumulationModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int index) {
            if (model) {
              model->setAccumulationMode(static_cast<StatisticsModel::AccumulationMode>(index));
              updateZoneCountLabels();
            }
          });

  connect(filterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (model) {
      model->setFilterText(text);
      updateZoneCountLabels();
    }
  });

  connect(tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
          [this](int logicalIndex, Qt::SortOrder order) {
            model->sort(logicalIndex, order);
            updateZoneCountLabels();
          });

  connect(clearFilterButton, &QPushButton::clicked, this, [this]() { filterEdit->clear(); });

  connect(framesView, &FramesView::statRangeChanged, this, &StatisticsView::onStatgeRangeChanged);

  connect(limitRangeBtn, &QPushButton::toggled, this, &StatisticsView::onLimitRangeToggled);

  connect(tableView, &QTableView::customContextMenuRequested, this, &StatisticsView::showContentMenu);

}

void StatisticsView::setupTableView() {
  tableView->setMouseTracking(true);
  tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableView->setSelectionMode(QAbstractItemView::SingleSelection);
  tableView->setSortingEnabled(true);
  tableView->setStyleSheet(
    "QTableView { background-color: #2D2D2D; color: white; gridline-color: #404040; }"
    "QTableView::item:selected { background-color: #505050; color: white; }"
    "QHeaderView::section { color: white; background-color: #2D2D2D; border: 1px solid #404040; }"
  );
  tableView->horizontalHeader()->setStretchLastSection(true);
  tableView->horizontalHeader()->setSortIndicatorShown(true);
  tableView->horizontalHeader()->setSectionsMovable(true);
  tableView->verticalHeader()->hide();
  tableView->setModel(model);

  //set delegate
  auto myDelegate = new StatisticsDelegate(model, view);
  tableView->setItemDelegate(myDelegate);
  tableView->setShowGrid(true);
  tableView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  tableView->setContextMenuPolicy(Qt::CustomContextMenu);
  updateColumnSizes();
}

void StatisticsView::viewSource(const char* fileName, int line) {
  assert(fileName);
  srcViewFile = fileName;
  model->openSource(fileName, line, worker, view);

  if(!srcView) {
    srcView = new SourceView(nullptr);
    srcView->setAttribute(Qt::WA_DeleteOnClose);
    srcView->setStyleSheet("background-color: #2D2D2D;");
    connect(srcView, &QObject::destroyed, this, [this](){
        srcView = nullptr;
    });
  }

  const auto& source = model->getSource();
  if(!source.empty()) {
    QString content = QString::fromStdString(std::string(source.data(), source.dataSize()));
    srcView->setWindowTitle(QString("Source: %1").arg(fileName));
    srcView->loadSource(content, line);
    srcView->show();
    srcView->raise();
    srcView->activateWindow();
  }
}

bool StatisticsView::srcFileValid(const char* fn, uint64_t olderThan, const tracy::Worker& worker,
                                  View* view) {
  if (worker.GetSourceFileFromCache(fn).data != nullptr) return true;
  struct stat buf;
  if (stat(view->sourceSubstitution(fn), &buf) == 0 && (buf.st_mode & S_IFREG) != 0) {
    if (!view->validateSourceAge()) return true;
    return (uint64_t)buf.st_mtime < olderThan;
  }
  return false;
}

void StatisticsView::showContentMenu(const QPoint& pos) {
  QModelIndex index = tableView->indexAt(pos);
  if(!index.isValid() || index.column() != StatisticsModel::LocationColumn) {
    return;
  }

  auto& srcloc = model->getSrcLocFromIndex(tableView->model()->index(index.row(), StatisticsModel::NameColumn));
  const char* fileName = worker.GetString(srcloc.file);
  int line  = static_cast<int>(srcloc.line);

  QMenu menu(this);
  QAction* viewSrcAction = menu.addAction(tr("view source"));

  connect(viewSrcAction, &QAction::triggered, this, [this, fileName, line]() {
    viewSource(fileName, line);
  });

  menu.exec(tableView->viewport()->mapToGlobal(pos));
}

void StatisticsView::updateColumnSizes() {
  if (!model) return;

  tableView->setColumnWidth(StatisticsModel::Column::NameColumn, 500);
  tableView->setColumnWidth(StatisticsModel::Column::LocationColumn, 400);
  tableView->setColumnWidth(StatisticsModel::Column::TotalTimeColumn, 120);
  tableView->setColumnWidth(StatisticsModel::Column::CountColumn, 80);
  tableView->setColumnWidth(StatisticsModel::Column::MtpcColumn, 100);
  tableView->setColumnWidth(StatisticsModel::Column::ThreadCountColumn, 30);

  tableView->horizontalHeader()
  ->setSectionResizeMode(StatisticsModel::Column::NameColumn, QHeaderView::Interactive);
  tableView->horizontalHeader()
  ->setSectionResizeMode(StatisticsModel::Column::LocationColumn, QHeaderView::Interactive);
  tableView->horizontalHeader()
  ->setSectionResizeMode(StatisticsModel::Column::TotalTimeColumn, QHeaderView::Interactive);
  tableView->horizontalHeader()
  ->setSectionResizeMode(StatisticsModel::Column::CountColumn, QHeaderView::Interactive);
  tableView->horizontalHeader()
  ->setSectionResizeMode(StatisticsModel::Column::MtpcColumn, QHeaderView::Interactive);
  tableView->horizontalHeader()
  ->setSectionResizeMode(StatisticsModel::Column::ThreadCountColumn, QHeaderView::Interactive);

  int totalWidth = 500 + 400 + 120 + 80 + 100 + 30;
  tableView->setMinimumWidth(totalWidth);

}

void StatisticsView::updateZoneCountLabels() {
  if (model) {
    mtotalZonesLabel->setText(QString::number(model->getTotalZoneCount()));
    mvisibleZonesLabel->setText(QString::number(model->getVisibleZoneCount()));
  }
}

void StatisticsView::onStatgeRangeChanged(int64_t start, int64_t end, bool active) {
  if(limitRangeBtn->isChecked()) {
    view->m_statRange.min = start;
    view->m_statRange.max = end;
    model->setStatRange(start, end, active);
    model->refreshData();
    updateZoneCountLabels();
  }
}

//limit range
 void StatisticsView::onLimitRangeToggled(bool active) {
   if(model) {
     if(active) {
       view->m_statRange.active = true;
       view->m_statRange.min = viewData.zvStart;
       view->m_statRange.max = viewData.zvEnd;
       model->setStatRange(view->m_statRange.min, view->m_statRange.max, true);
       //model->refreshData();
      }
      else {
        view->m_statRange.active = false;
        model->setStatRange(0, 0, false);
        //model->refreshData();
      }
      updateZoneCountLabels();
    }
 }
