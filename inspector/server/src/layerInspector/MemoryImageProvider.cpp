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

#include "MemoryImageProvider.h"

namespace inspector {
MemoryImageProvider::MemoryImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {
  defaultImage = new QImage(200, 200, QImage::Format_RGBA8888);
  defaultImage->fill(QColor(56, 56, 56));
}

MemoryImageProvider::~MemoryImageProvider() {
  if (defaultImage) {
    delete defaultImage;
  }
}

void MemoryImageProvider::setImage(uint64_t id, int width, int height, const QByteArray& rawData) {
  rwLock.lockForWrite();
  imageMap[id] = {width, height, rawData};
  rwLock.unlock();
  emit imageFlush(id);
}

void MemoryImageProvider::clearImageMap() {
  rwLock.lockForWrite();
  imageMap.clear();
  rwLock.unlock();
}

void MemoryImageProvider::setCurrentImageID(uint64_t id) {
  currentImageID = id;
}

bool MemoryImageProvider::isImageExisted(uint64_t id) {
  rwLock.lockForRead();
  bool result = imageMap.find(id) != imageMap.end();
  rwLock.unlock();
  return result;
}

QImage MemoryImageProvider::requestImage(const QString& id, QSize* size,
                                         const QSize& requestedSize) {
  Q_UNUSED(requestedSize);
  uint64_t idn = id.split("-")[0].toULongLong();
  rwLock.lockForRead();
  if (imageMap.find(idn) != imageMap.end()) {
    rwLock.unlock();
    if (size) {
      *size = QSize(imageMap[idn].width, imageMap[idn].height);
    }
    return imageMap[idn].createImage();
  }
  rwLock.unlock();
  if (size) {
    *size = defaultImage->size();
  }
  return defaultImage->copy();
}
}  // namespace inspector