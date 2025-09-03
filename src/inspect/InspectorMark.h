/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#ifdef TGFX_USE_INSPECTOR

#include "FrameCapture.h"
#include "FunctionTimer.h"
#include "LayerTree.h"

#define SEND_LAYER_DATA(data) tgfx::inspect::LayerTree::SocketAgent::Get().setData(data)
#define LAYER_CALLBACK(func) tgfx::inspect::LayerTree::SocketAgent::Get().setCallBack(func)
#define SET_DISPLAY_LIST(display) tgfx::inspect::LayerTree::Get().setDisplayList(display)
#define RENDER_VISABLE_OBJECT(context) tgfx::inspect::LayerTree::Get().renderImageAndSend(context)
#define SET_SLECTED_LAYER(layer) tgfx::inspect::LayerTree::Get().setSelectLayer(layer)

#define FRAME_MARK tgfx::inspect::FrameCapture::SendFrameMark(nullptr)
#define FUNCTION_MARK(type, active) tgfx::inspect::FunctionTimer functionTimer(type, active)
#define OPERATE_MARK(type) FUNCTION_MARK(type, true)
#define TASK_MARK(type) FUNCTION_MARK(type, true)
#define ATTRIBUTE_NAME(name, value) tgfx::inspect::FrameCapture::SendAttributeData(name, value)
#define ATTRIBUTE_NAME_ENUM(name, value, type)                                      \
  tgfx::inspect::FrameCapture::SendAttributeData(name, static_cast<uint8_t>(value), \
                                                 static_cast<uint8_t>(type))

#define ATTRIBUTE_ENUM(value, type) ATTRIBUTE_NAME_ENUM(#value, value, type)

#define CAPUTRE_PIXELS_DATA(texturePtr, width, height, rowBytes, format, pixels)               \
  auto frameCapture = inspect::FrameCaptureTexture::MakeFrom(texture, width, height, rowBytes, \
                                                             pixelFormat, pixels);             \
  tgfx::inspect::FrameCapture::SendFrameCaptureTexture(frameCapture, false);

#define CAPUTRE_TEXTURE(commandQueue, texture)                                                  \
  auto frameCapture = inspect::FrameCaptureTexture::MakeFrom(textureView->getTexture(), queue); \
  tgfx::inspect::FrameCapture::SendFrameCaptureTexture(frameCapture, false);

#define CAPUTRE_RENDER_TARGET(renderTarget)                                              \
  if (inspect::FrameCapture::CurrentFrameShouldCaptrue()) {                              \
    auto frameCapture = inspect::FrameCaptureTexture::MakeFrom(renderTarget);            \
    tgfx::inspect::FrameCapture::SendFrameCaptureTexture(std::move(frameCapture), true); \
    tgfx::inspect::FrameCapture::SendOutputTextureID(renderTarget->getRenderTexture());  \
  }

#define CAPUTRE_FRARGMENT_PROCESSORS(fragmentProcessors) \
  tgfx::inspect::FrameCapture::SendFragmentProcessor(fragmentProcessors)

#define SEND_OUTPUT_TEXUTRE_ID(texturePtr) \
  tgfx::inspect::FrameCapture::SendOutputTextureID(texturePtr)

#else

#define SEND_LAYER_DATA(data) (void)data
#define LAYER_CALLBACK(func) (void)func
#define SET_DISPLAY_LIST(display) (void)display
#define RENDER_VISABLE_OBJECT(context) (void)context
#define SET_SLECTED_LAYER(layer) (void)layer
#define FRAME_MARK
#define FUNCTION_MARK(type, active)
#define OPERATE_MARK(type)
#define TASK_MARK(type)
#define ATTRIBUTE_NAME(name, value)
#define ATTRIBUTE_NAME_ENUM(name, value, type)
#define ATTRIBUTE_ENUM(value, type)
#define CAPUTRE_PIXELS_DATA(texturePtr, width, height, rowBytes, format, pixels)
#define CAPUTRE_TEXTURE(commandQueue, texturePtr)
#define CAPUTRE_FRARGMENT_PROCESSORS(pipline)
#define SEND_OUTPUT_TEXUTRE_ID(texturePtr)
#define CAPUTRE_RENDER_TARGET(renderTarget)

#endif