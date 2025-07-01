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

#include <QByteArray>
#include <QImage>
#include <QQuickImageProvider>
#include <QReadWriteLock>
#include <unordered_map>

namespace inspector {
struct ImageData {
  int width;
  int height;
  QByteArray data;
  QImage createImage() {
    auto img = QImage((uchar*)data.data(), width, height, QImage::Format_RGBA8888);
    return img;
  }
};

class MemoryImageProvider : public QQuickImageProvider {
  Q_OBJECT
 public:
  MemoryImageProvider();
  ~MemoryImageProvider() override;

  void setImage(uint64_t id, int width, int height, const QByteArray& rawData);
  void clearImageMap();
  void setCurrentImageID(uint64_t id);
  uint64_t ImageID() const {
    return currentImageID;
  }
  bool isImageExisted(uint64_t id);
  // Implement the requestImage method
  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
 signals:
  void imageFlush(uint64_t imgID);

 private:
  std::unordered_map<uint64_t, ImageData> imageMap;
  uint64_t currentImageID;
  QReadWriteLock rwLock;
  QImage* defaultImage;
};
}  // namespace inspector
