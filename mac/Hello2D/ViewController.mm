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

#import "ViewController.h"
#import "TGFXView.h"
#import <QuartzCore/CVDisplayLink.h>
#include <iostream>

@interface ViewController ()
@property(strong, nonatomic) TGFXView* tgfxView;
@property(nonatomic) int drawCount;
@end

CVDisplayLinkRef displayLink;

@implementation ViewController

//static int frames = 0;
//static uint64_t startMs = 0;

static CVReturn OnAnimationCallback( CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
                                   CVOptionFlags, CVOptionFlags*, void* userInfo) {
  auto view_controller = (__bridge ViewController*)userInfo;
  //[view_controller redraw];
    
    
    dispatch_queue_t mainQueue = dispatch_get_main_queue();
           // 将需要在主线程执行的任务封装到 block 中
           dispatch_async(mainQueue, ^{
               //[view_controller updateTitle];
               [view_controller redraw];
    });
    
//    auto now = std::chrono::system_clock::now();
//    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
//    auto ms = now_ms.time_since_epoch().count();
//    if (frames <= 0) {
//        startMs = ms;
//    }
//    ++frames;
//    if (ms - startMs > 1000) {
//        dispatch_queue_t mainQueue = dispatch_get_main_queue();
//        // 将需要在主线程执行的任务封装到 block 中
//        dispatch_async(mainQueue, ^{
//            [view_controller updateTitle];
//        });
//        frames = 0;
//        startMs = ms;
//    }
   
  return kCVReturnSuccess;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tgfxView = [[TGFXView alloc] initWithFrame:self.view.bounds];
  [self.tgfxView setAutoresizingMask:kCALayerWidthSizable | kCALayerHeightSizable];
  [self.view addSubview:self.tgfxView];
  [self.tgfxView draw:0];
    CVDisplayLinkCreateWithActiveCGDisplays(&displayLink);
    CVDisplayLinkSetOutputCallback(displayLink, &OnAnimationCallback,  (__bridge void*)self);
    
    
    

}

- (void)viewDidLayout {
  [super viewDidLayout];
  [self.tgfxView draw:0];
    CVDisplayLinkStart(displayLink);
    self.tgfxView.window.title = @"TGFX";
    
}

- (void)mouseUp:(NSEvent*)event {
  self.drawCount++;
  [self.tgfxView draw:self.drawCount];
}


- (void)redraw {
    [self.tgfxView draw:0];
   
}

- (void)updateTitle{
    
//    self.tgfxView.window.title = [self.tgfxView fpsInfo];
}




@end
