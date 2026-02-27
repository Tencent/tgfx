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

#pragma once

#include "tgfx/core/Canvas.h"
#include "tgfx/core/Picture.h"

namespace tgfx {
class PictureContext;

/**
 * The PictureRecorder class records drawing commands made to a Canvas and generates a Picture
 * object that captures all these commands, allowing them to be replayed at a later time.
 */
class PictureRecorder {
 public:
  ~PictureRecorder();

  /**
   * Begins recording drawing commands. If recording is already active, it clears the existing
   * commands and starts afresh. Returns the canvas that captures the drawing commands. The returned
   * Canvas is managed by the PictureRecorder and is deleted when the PictureRecorder is deleted.
   */
  Canvas* beginRecording();

  /**
   * Returns the recording canvas if one is active, or NULL if recording is not active.
   */
  Canvas* getRecordingCanvas() const;

  /**
   * Signals that the caller is done recording and returns a Picture object that captures all the
   * drawing commands made to the canvas. Returns nullptr if no recording is active or no commands
   * were recorded.
   */
  std::shared_ptr<Picture> finishRecordingAsPicture();

 private:
  bool activelyRecording = false;
  PictureContext* pictureContext = nullptr;
  Canvas* canvas = nullptr;
};
}  // namespace tgfx
