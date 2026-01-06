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
#include "FunctionStat.h"
#include "LayerTree.h"
#include "Protocol.h"

#define SEND_LAYER_DATA(data) tgfx::inspect::LayerTree::SocketAgent::Get().setData(data)
#define LAYER_CALLBACK(func) tgfx::inspect::LayerTree::SocketAgent::Get().setCallBack(func)
#define SET_DISPLAY_LIST(display) tgfx::inspect::LayerTree::Get().setDisplayList(display)
#define RENDER_VISABLE_OBJECT(context) tgfx::inspect::LayerTree::Get().renderImageAndSend(context)
#define SET_SLECTED_LAYER(layer) tgfx::inspect::LayerTree::Get().setSelectLayer(layer)

#define MARE_CONCAT(x, y) MARE_CONCAT_INDIRECT(x, y)
#define MARE_CONCAT_INDIRECT(x, y) x##y
#define MARK_LINE __LINE__
#define FRAME_MARK tgfx::inspect::FrameCapture::GetInstance().sendFrameMark(nullptr)
#define FUNCTION_MARK(type, active) \
  tgfx::inspect::FunctionStat MARE_CONCAT(functionTimer, MARK_LINE) = {type, active}
#define OPERATE_MARK(type) \
  FUNCTION_MARK(tgfx::inspect::DrawOpTypeToOpTaskType[static_cast<uint8_t>(type)], true)
#define TASK_MARK(type) FUNCTION_MARK(type, true)
#define ATTRIBUTE_NAME(name, value) \
  tgfx::inspect::FrameCapture::GetInstance().sendAttributeData(name, value)
#define ATTRIBUTE_NAME_ENUM(name, value, type)                                                    \
  tgfx::inspect::FrameCapture::GetInstance().sendAttributeData(name, static_cast<uint8_t>(value), \
                                                               static_cast<uint8_t>(type))

#define ATTRIBUTE_ENUM(value, type) ATTRIBUTE_NAME_ENUM(#value, value, type)

#define CAPUTRE_RENDER_TARGET(renderTarget) \
  tgfx::inspect::FrameCapture::GetInstance().captureRenderTarget(renderTarget)

#define CAPUTRE_FRARGMENT_PROCESSORS(context, colors, coverages) \
  tgfx::inspect::FrameCapture::GetInstance().sendFragmentProcessor(context, colors, coverages);

#define PROGRAM_KEY(programKey) \
  tgfx::inspect::FrameCapture::GetInstance().sendProgramKey(programKey)

#define CAPUTRE_PROGRAM_INFO(programKey, context, programInfo) \
  tgfx::inspect::FrameCapture::GetInstance().captureProgramInfo(programKey, context, programInfo);

#define UNIFORM_VALUE(name, data, size) \
  tgfx::inspect::FrameCapture::GetInstance().sendUniformValue(name, data, size)

#define DRAW_OP(drawOp) tgfx::inspect::FrameCapture::GetInstance().sendOpPtr(drawOp)

#define CAPUTRE_RECT_MESH(drawOp, provider) \
  tgfx::inspect::FrameCapture::GetInstance().sendRectMeshData(drawOp, provider)

#define CAPUTRE_RRECT_MESH(drawOp, provider) \
  tgfx::inspect::FrameCapture::GetInstance().sendRRectMeshData(drawOp, provider)

#define CAPUTRE_SHAPE_MESH(drawOp, styledShape, aaType, clipBounds)                         \
  tgfx::inspect::FrameCapture::GetInstance().sendShapeMeshData(drawOp, styledShape, aaType, \
                                                               clipBounds)

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
#define CAPUTRE_RENDER_TARGET(renderTarget)
#define CAPUTRE_FRARGMENT_PROCESSORS(context, colors, coverages)
#define PROGRAM_KEY(programKey)
#define CAPUTRE_PROGRAM_INFO(programKey, context, programInfo)
#define UNIFORM_VALUE(name, data, size)
#define DRAW_OP(drawOp)
#define CAPUTRE_RECT_MESH(drawOp, provider)
#define CAPUTRE_RRECT_MESH(drawOp, provider)
#define CAPUTRE_SHAPE_MESH(drawOp, styledShape, aaType, clipBounds)

#endif
