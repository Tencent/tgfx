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

#import <Cocoa/Cocoa.h>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/gpu/opengl/cgl/CGLWindow.h"

@interface TGFXView : NSView

@property(nonatomic) int drawIndex;
@property(nonatomic) float zoomScale;
@property(nonatomic) CGPoint contentOffset;
@property(nonatomic) CVDisplayLinkRef cvDisplayLink;
@property(nonatomic, strong) CADisplayLink* caDisplayLink;

- (bool)draw;
- (void)startDisplayLink;
- (void)stopDisplayLink;
- (void)markDirty;

@end
