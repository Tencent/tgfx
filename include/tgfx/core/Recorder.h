/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Canvas.h"
#include "tgfx/core/Picture.h"

namespace tgfx {
class RecordingContext;

/**
 * The Recorder class records drawing commands made to a Canvas and generates a Picture object that
 * captures all these commands, allowing them to be replayed at a later time.
 */
class Recorder {
 public:
  ~Recorder();

  /**
   * Returns a Canvas that records the drawing commands. If the recording is already active, the
   * same Canvas object is returned. The returned Canvas is managed and owned by Recorder, and is
   * deleted when the Recorder is deleted.
   */
  Canvas* getCanvas();

  /**
   * Signals that the caller is done recording and returns a Picture object that captures all the
   * drawing commands made to the canvas. After this method is called, the Recorder is reset and can
   * be used to record new commands. Returns nullptr if no recording is active or no commands were
   * recorded.
   */
  std::shared_ptr<Picture> finishRecordingAsPicture();

 private:
  RecordingContext* recordingContext = nullptr;
  Canvas* canvas = nullptr;
};
}  // namespace tgfx
