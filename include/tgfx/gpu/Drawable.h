/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <memory>
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

class Context;
class RenderTargetProxy;
class Window;

/**
 * Drawable represents the usage right of a single displayable frame of a Window. It carries only
 * the frame identity and its delivery state: it holds no drawing Context and never references a
 * Surface. The frame is imported into a Context via Surface::MakeFrom(context, drawable), which
 * resolves backend targets that need a current context, and is presented via
 * Context::present(drawable). Readback goes through Surface::asyncReadPixels() until a
 * presentation is registered or the frame is presented, and only if the frame's window supports
 * readback (see Window::supportsReadback()). Dropping a Drawable without presenting it discards
 * the frame; backends release it without any GPU calls.
 */
class Drawable {
 public:
  virtual ~Drawable() = default;

  /**
   * Returns the width of the frame in pixels.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the frame in pixels.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns the color space associated with this frame, inherited from its Window. Returns
   * nullptr for the default sRGB.
   */
  std::shared_ptr<ColorSpace> colorSpace() const {
    return _colorSpace;
  }

 protected:
  Drawable(int width, int height, std::shared_ptr<ColorSpace> colorSpace);

  /**
   * Imports the frame target into the given Context. Called by Surface::MakeFrom() while the
   * Context is locked and current, so backends that need a current context (GL, EAGL) resolve
   * their targets here. Returns nullptr if the frame cannot be imported right now.
   */
  virtual std::shared_ptr<RenderTargetProxy> onImport(Context* context) = 0;

  /**
   * Called while the DrawingBuffer that carries the frame's rendering commands is about to be
   * submitted — for every undelivered frame, whether or not a presentation has been registered.
   * Backends whose render submission must synchronize with the frame acquisition (Vulkan's
   * imageAvailable semaphore wait) override this to wire that synchronization into the upcoming
   * submission. The default implementation does nothing.
   */
  virtual void onAttachSubmission(Context* context);

  /**
   * Called when the DrawingBuffer that carries the frame's rendering commands is about to be
   * submitted, after a presentation was requested via Context::present() while the rendering was
   * still unsubmitted. Returns true when the presentation is attached to that submission itself
   * (for example encoded into its command buffer), in which case the submit pipeline only marks
   * the frame presented. The default implementation returns false, which defers to onPresent()
   * at the end of that submission. Binding the scheduling to the frame's own drawing buffer
   * keeps the presentation ordered after the frame's rendering even when unrelated recordings
   * are submitted in between.
   */
  virtual bool onSchedulePresent(Context* context);

  /**
   * Presents the frame. The frame's rendering commands have been submitted; the presentation must
   * be ordered after them without blocking the CPU. The Context is still valid and current.
   */
  virtual void onPresent(Context* context) = 0;

  /**
   * Discards the frame without presenting it. No GPU calls are allowed; backends only release
   * CPU-side references and mark state for later recovery.
   */
  virtual void onAbandon();

  /**
   * Must be invoked by subclass destructors to discard an undelivered frame, since the base class
   * cannot dispatch to onAbandon() while it is being destroyed.
   */
  void abandon();

  /**
   * Marks the frame as having a registered presentation request. Used by internal automatic-path
   * frames (WindowFrame), whose presentation is registered at construction instead of through
   * Context::present().
   */
  void markPresentationRequested();

  std::shared_ptr<Window> _window = nullptr;
  std::shared_ptr<RenderTargetProxy> _importedTarget = nullptr;

 private:
  friend class Context;
  friend class DrawableSurface;
  friend class DrawingBuffer;
  friend class Surface;
  friend class Window;

  enum class Delivery {
    Acquired,
    Imported,
    Submitted,
    PresentRequested,
    Presented,
    Abandoned,
  };

  // Imports the frame while the Context is locked. Returns nullptr if the frame was already
  // imported or the import failed.
  std::shared_ptr<RenderTargetProxy> import(Context* context);

  // Returns true when readback may still be scheduled for this frame.
  bool canReadBack() const;

  // Registers or executes a presentation request. Returns false on an invalid delivery state.
  bool requestPresent(Context* context);

  // Called while the drawing buffer that collected this frame is being submitted: schedules the
  // presentation onto that submission for frames whose presentation was requested before their
  // rendering was submitted.
  void scheduleIfRequested(Context* context);

  // Marks the frame as submitted; presents it if a presentation was requested earlier.
  void onSubmissionCompleted(Context* context);

  // Drops the strong references that are only needed while the frame is undelivered.
  void releaseFrameHandles();

  bool _presentationAttached = false;
  Delivery _delivery = Delivery::Acquired;
  int _width = 0;
  int _height = 0;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};

}  // namespace tgfx
