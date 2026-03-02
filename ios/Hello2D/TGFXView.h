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

#import <UIKit/UIKit.h>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#ifdef TGFX_USE_METAL
#include "tgfx/gpu/metal/MetalWindow.h"
#else
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/gpu/opengl/eagl/EAGLWindow.h"
#endif
#include "tgfx/layers/DisplayList.h"

@interface TGFXView : UIView

- (void)updateLayerTree:(int)index;
- (void)updateZoomScaleAndOffset:(float)zoom offset:(CGPoint)offset;
- (void)draw;

@end
