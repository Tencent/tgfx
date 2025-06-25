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

#include "InspectorEvent.h"

namespace inspector {

OpOrTask getOpTaskType(OpTaskType type) {
  switch (type) {
    case TextureUploadTask:
    case ShapeBufferUploadTask:
    case GpuUploadTask:
    case TextureCreateTask:
    case RenderTargetCreateTask:
    case TextureFlattenTask:
    case RenderTargetCopyTask:
    case RuntimeDrawTask:
    case TextureResolveTask:
      return OpOrTask::Task;
    case ClearOp:
    case RectDrawOp:
    case RRectDrawOp:
    case ShapeDrawOp:
    case DstTextureCopyOp:
    case ResolveOp:
      return OpOrTask::Op;
    default:
      return OpOrTask::NoType;
  }
}
}  // namespace inspector