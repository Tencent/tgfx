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

#include "View.h"
#include <QGroupBox>
#include <QLabel>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <src/profiler/TracyFileselector.hpp>
#include "FramesView.h"
#include "TimelineView.h"
#include "TracySysUtil.hpp"
#include "MainView.h"

static const char* compressionName[] = {
  "LZ4",
  "LZ4 HC",
  "LZ4 HC extreme",
  "Zstd",
  nullptr
};
static const char* compressionDesc[] = {
  "Fastest save, fast load time, big file size",
  "Slow save, fastest load time, reasonable file size",
  "Very slow save, fastest load time, file smaller than LZ4 HC",
  "Configurable save time (fast-slowest), reasonable load time, smallest file size",
  nullptr
};

SaveFileDialog::SaveFileDialog(std::string& filename, QWidget* parent)
  : filename(filename)
  , QDialog(parent) {
  initWidget();
  initConnect();
}

QSpinBox* SaveFileDialog::createSpinBox(int min, int max, int step, int defaultValue) {
  auto spinBox = new QSpinBox;
  spinBox->setRange(min, max);
  spinBox->setSingleStep(step);
  spinBox->setValue(defaultValue);
  return spinBox;
}

void SaveFileDialog::initWidget() {
  setFixedSize(400, 300);
  setStyleSheet("background-color: grey;");
  auto layout = new QVBoxLayout(this);

  auto textLable = new QLabel(this);
  QString path;
  QTextStream str(&path);
  str << "Path: " << filename.c_str();
  textLable->setText(path);
  layout->addWidget(textLable);

  auto compressionGroup = new QGroupBox(tr("Trace compression"), this);
  compressionGroup->setToolTip("Can be changed later with the upgrade utility");
  compressionLayout = new QVBoxLayout;
  int idx = 0;
  while (compressionName[idx]) {
    auto radioButton = new QRadioButton(QString(compressionName[idx]), this);
    radioButton->setToolTip(compressionDesc[idx]);
    compressionLayout->addWidget(radioButton);
    idx++;
  }
  static_cast<QRadioButton*>(compressionLayout->itemAt(int(3))->widget())->setChecked(true);
  compressionGroup->setLayout(compressionLayout);
  layout->addWidget(compressionGroup);

  auto zstdLayout = new QHBoxLayout(this);
  auto zstdLable = new QLabel("Zstd level", this);
  zstdLable->setToolTip("Increasing level decreases file size, but increases save and load times");
  zstdSpinBox = createSpinBox(1, 22, 1, 3);
  zstdLayout->addWidget(zstdLable);
  zstdLayout->addWidget(zstdSpinBox);
  layout->addLayout(zstdLayout);

  auto streamLayout = new QHBoxLayout(this);
  auto streamLable = new QLabel("Compression streams", this);
  streamLable->setToolTip("Parallelize save and load at the cost of file size");
  streamSpinBox = createSpinBox(1, 64, 1, 4);
  streamLayout->addWidget(streamLable);
  streamLayout->addWidget(streamSpinBox);
  layout->addLayout(streamLayout);

  auto buttonLayout = new QHBoxLayout(this);
  confirmButton = new QPushButton("Save trace", this);
  cancelButton = new QPushButton("Cancel", this);
  buttonLayout->addWidget(confirmButton);
  buttonLayout->addWidget(cancelButton);
  layout->addLayout(buttonLayout);
  // layout->addLayout(buttonLayout, 0, Qt::AlignLeft | Qt::AlignBottom);
}

void SaveFileDialog::getValue(std::string& fn, int& compressionChosse, int& zstdLevel, int& compressionStreams) {
  int i = 0;
  while(compressionName[i]) {
    if (static_cast<QRadioButton*>(compressionLayout->itemAt(int(i))->widget())->isChecked()) {
      compressionChosse = i;
    }
    ++i;
  }
  zstdLevel = zstdSpinBox->value();
  compressionStreams = streamSpinBox->value();
  fn = filename;
}

void SaveFileDialog::initConnect(){
  connect(zstdSpinBox, &QSpinBox::valueChanged, this, &SaveFileDialog::zstdLevelChanged);
  connect(confirmButton, &QPushButton::clicked, (View*)parentWidget(), &View::save);
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::close);
}

void SaveFileDialog::zstdLevelChanged(){
  int i = 0;
  while (compressionName[i]) {
    if (i != 3 &&  static_cast<QRadioButton*>(compressionLayout->itemAt(int(i))->widget())->isChecked()) {
      static_cast<QRadioButton*>(compressionLayout->itemAt(int(i))->widget())->setChecked(false);
    }
    ++i;
  }
  static_cast<QRadioButton*>(compressionLayout->itemAt(int(3))->widget())->setChecked(true);
}

View::View(const char* addr, uint16_t port, int width, const tracy::Config& config, QWidget* parent)
  : width(width)
  , worker(addr, port, config.memoryLimit == 0 ? -1 :
    ( config.memoryLimitPercent * tracy::GetPhysicalMemorySize() / 100 ) )
  , config(config)
  , QWidget(parent)
{
  initConnect();
  initView();
}

View::View(tracy::FileRead& file, int width, const tracy::Config& config, QWidget* parent)
  : width(width)
  , worker(file)
  , userData(worker.GetCaptureProgram().c_str(), worker.GetCaptureTime())
  , config(config)
  , QWidget(parent) {
  initConnect();
  initView();
  userData.StateShouldBePreserved();
  userData.LoadState(viewData);
}

View::~View() {
  userData.SaveState(viewData);
}

void View::initConnect() {
  connect(this, &View::closeView, (MainView*)parentWidget(), &MainView::discardConnect);
}

void View::initView() {
  if (!worker.HasData()) {
    timerId = startTimer(1);
    connectDialog = new QDialog;
    auto layout = new QVBoxLayout(connectDialog);
    auto textLable = new QLabel(connectDialog);
    textLable->setAlignment(Qt::AlignCenter);
    textLable->setText("Waiting for connect...");
    layout->addWidget(textLable);
    connectDialog->exec();
  }
  if (!worker.HasData()) {
    Q_EMIT closeView();
    return;
  }
  connected = true;
  ViewImpl();
}

void View::saveFile() {
  auto cb = [this]( const char* fn ) {
    const auto sz = strlen( fn );
    if( sz < 7 || memcmp( fn + sz - 6, ".tracy", 6 ) != 0 )
    {
      char tmp[1024];
      sprintf( tmp, "%s.tracy", fn );
      filenameStaging = tmp;
    }
    else
    {
      filenameStaging = fn;
    }
  };
  tracy::Fileselector::SaveFile( "tracy", "Tracy Profiler trace file", cb );

  if (!filenameStaging.empty()) {
    saveFileDialog = new SaveFileDialog(filenameStaging, this);
    saveFileDialog->move(width / 2, height() / 2);
    saveFileDialog->exec();
  }
}

bool View::save() {
  if (!saveFileDialog) {
    return false;
  }

  std::string fn;
  int zlevel, streams, intComp;
  saveFileDialog->getValue(fn, intComp, zlevel, streams);
  auto comp = tracy::FileCompression(intComp);
  std::unique_ptr<tracy::FileWrite> f(tracy::FileWrite::Open(fn.c_str(), comp, zlevel, streams));
  if(!f) {
    return false;
  }
  userData.StateShouldBePreserved();
  saveThread = std::thread([this, f{std::move(f)}] {
      std::lock_guard<std::mutex> lock(worker.GetDataLock());
      worker.Write(*f, false);
      f->Finish();
  });
  saveThread.join();
  filenameStaging = "";
  delete saveFileDialog;
  return true;
}

void View::ViewImpl() {
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  qmlRegisterType<FramesView>("Frames", 1, 0, "FramesView");
  framesEngine = new QQmlApplicationEngine(QUrl(QStringLiteral("qrc:/qml/Frames.qml")));
  framesEngine->rootContext()->setContextProperty("_worker", (unsigned long long)&worker);
  framesEngine->rootContext()->setContextProperty("_viewData", &viewData);
  auto quickWindow = static_cast<QQuickWindow*>(framesEngine->rootObjects().value(0));
  auto framesWidget = createWindowContainer(quickWindow);
  framesWidget->setFixedHeight(50);

  qmlRegisterType<tracy::Worker>("tracy", 1, 0, "TracyWorker");
  qmlRegisterType<TimelineView>("Timeline", 1, 0, "TimelineView");
  timelineEngine = new QQmlApplicationEngine(QUrl(QStringLiteral("qrc:/qml/Timeline.qml")));
  timelineEngine->rootContext()->setContextProperty("_worker", (unsigned long long)&worker);
  timelineEngine->rootContext()->setContextProperty("_viewData", &viewData);
  quickWindow = static_cast<QQuickWindow*>(timelineEngine->rootObjects().value(0));
  auto timelineWidget = createWindowContainer(quickWindow);
  timelineWidget->resize(1000, 1000);

  layout->addWidget(framesWidget);

  layout->addWidget(timelineWidget);
}

void View::timerEvent(QTimerEvent* event) {
  if (worker.HasData() && connectDialog) {
    connectDialog->close();
    killTimer(timerId);
  }
}
