/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Recorder.h"
#include "core/RecordingContext.h"

namespace tgfx {
Recorder::~Recorder() {
  delete canvas;
  delete recordingContext;
}

Canvas* Recorder::beginRecording() {
  if (canvas == nullptr) {
    recordingContext = new RecordingContext();
    canvas = new Canvas(recordingContext, nullptr, optimizeMemory);
  } else {
    canvas->resetStateStack();
    recordingContext->clear();
  }
  activelyRecording = true;
  return getRecordingCanvas();
}

Canvas* Recorder::getRecordingCanvas() const {
  return activelyRecording ? canvas : nullptr;
}

std::shared_ptr<Picture> Recorder::finishRecordingAsPicture() {
  if (!activelyRecording) {
    return nullptr;
  }
  activelyRecording = false;
  return recordingContext->finishRecordingAsPicture(optimizeMemory);
}
}  // namespace tgfx
