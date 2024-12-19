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

#import "TGFXView.h"
#include <cmath>
#include <random>
#include "drawers/Drawer.h"
#include <mach/mach_time.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <deque>


constexpr int kTotalRectCount = 24800;
static int frames = 0;
static uint64_t startMs = 0;
bool fAnimateEnabled = true;
NSString* fFpsInfo = @"TGFX";
float fps = 0.f;
int curRectCount = 20000; //当前矩形个数
constexpr int kIncreaseSpan = 3000; //矩形增加时间间隔：单位ms
const int kIncreaseStep = 400; //每次增加多少个矩形
static uint64_t lastIncreaseMs = 0;//上次增加矩形的时间
float fWidth = 720;
float fHeight = 1024.f;
const int fpsFlushStep = 20;
static int accFrames = 0;
static bool clearWarmUp = false;
static uint64_t fpsFlushSpan = 333;

struct RectData {
    float width;
    float speed;
    float x;
    float y;
};


std::vector<RectData> fRects = {};

@implementation TGFXView {
  std::shared_ptr<tgfx::CGLWindow> window;
  std::unique_ptr<drawers::AppHost> appHost;
  tgfx::Context* context;
  tgfx::Paint redPaint;
  tgfx::Paint greenPaint;
  tgfx::Paint bluePaint;
  tgfx::Paint textPaint;
  int renderFrames;
  int64_t startTime;
}

- (instancetype)initWithFrame:(NSRect)frameRect{
    context = nullptr;
    redPaint.setColor(tgfx::Color::Red());
    redPaint.setAntiAlias(false);
    greenPaint.setColor(tgfx::Color::Green());
    greenPaint.setAntiAlias(false);
    bluePaint.setColor(tgfx::Color::Blue());
    bluePaint.setAntiAlias(false);
    renderFrames = 0;
    startTime = -1;
    textPaint.setColor(tgfx::Color{0.32f,0.42f,0.62f,0.3f});
    return [super initWithFrame:frameRect];
    
}

uint64_t GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return static_cast<uint64_t>(time);
}

- (void)setBounds:(CGRect)bounds {
  CGRect oldBounds = self.bounds;
  [super setBounds:bounds];
  if (oldBounds.size.width != bounds.size.width || oldBounds.size.height != bounds.size.height) {
    [self updateSize];
  }
}

- (void)setFrame:(CGRect)frame {
//    frame.size.width = fWidth;
//    frame.size.height = fHeight;
    //self.frame.size.width = fWidth;
    //self.frame.size.height = fHeight;
  CGRect oldRect = frame;
  [super setFrame:frame];
  if (oldRect.size.width != frame.size.width || oldRect.size.height != frame.size.height) {
    [self updateSize];
  }
}

- (void)initRects:(int) count{
      fRects.resize(kTotalRectCount);
      std::mt19937 rng(18);
      std::mt19937 rngSpeed(36);
      std::uniform_real_distribution<float> distribution(0, 1);
      std::uniform_real_distribution<float> speedDistribution(1, 2);
      for (size_t i = 0; i < kTotalRectCount; i++) {
          fRects[i].x = static_cast<float>(fWidth) * distribution(rng);
          fRects[i].y = static_cast<float>(fHeight) * distribution(rng);
          fRects[i].width = 10.0f + distribution(rng) * 40.0f;
          fRects[i].speed = speedDistribution(rngSpeed);
      }
}

- (void)drawRects: (tgfx::Canvas*) canvas{
    for (size_t i = 0; i < fRects.size(); i++) {
        auto item = fRects[i];
        auto rect = tgfx::Rect::MakeXYWH(item.x, item.y, item.width, item.width);
        if (i % 3 == 0) {
          canvas->drawRect(rect, redPaint);
        } else if (i % 3 == 1) {
          canvas->drawRect(rect, greenPaint);
        } else {
          canvas->drawRect(rect, bluePaint);
        }
   }
}

- (void)drawFPS:(tgfx::Canvas*) canvas{
    renderFrames++;
    auto currentTimeStamp = GetCurrentTimestamp();
    auto timeOffset = currentTimeStamp - startTime;
    if (timeOffset >= 1000) {
        auto fps = renderFrames * 1000.0 / timeOffset;
        fFpsInfo = [NSString stringWithFormat:@"Rectangles: %d, FPS:%.1f", kTotalRectCount, fps];
        renderFrames = 0;
        startTime = currentTimeStamp;
    }
    
    auto rect = tgfx::Rect::MakeWH(fWidth * 1.f, 48 * appHost->density());
    canvas->drawRect(rect, textPaint);
    
    auto clr = tgfx::Color::White();
    if (fps > 58.f) {
        clr = tgfx::Color::Green();
    }else if (fps > 28.f){
        clr = tgfx::Color({1.f, 1.f, 0.f,1.f});//Yellow
    }
    else{
        clr = tgfx::Color({1.f, 0.f, 1.f,1.f});//ColorMAGENTA
    }
    
    tgfx::Paint text_paint;
    text_paint.setColor(clr);
    auto typeface = appHost->getTypeface("default");
    tgfx::Font font(typeface, 40 * appHost->density());
    std::string info = [fFpsInfo UTF8String];
    canvas->drawSimpleText(info, 110 * appHost->density(), 38 * appHost->density(), font, text_paint);
}

- (void)animateRects{
    for (size_t i = 0; i < kTotalRectCount; i++) {
        fRects[i].x -= fRects[i].speed;
        if (fRects[i].x + fRects[i].width < 0) {
            fRects[i].x = fWidth;
        }
      }
}

- (void)updateSize {
  CGSize size = [self convertSizeToBacking:self.bounds.size];
  auto width = static_cast<int>(roundf(size.width));
  auto height = static_cast<int>(roundf(size.height));
    
  if (appHost == nullptr) {
    appHost = std::make_unique<drawers::AppHost>();
    NSString* imagePath = [[NSBundle mainBundle] pathForResource:@"bridge" ofType:@"jpg"];
    auto image = tgfx::Image::MakeFromFile(imagePath.UTF8String);
    appHost->addImage("bridge", image);
    auto typeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
    appHost->addTypeface("default", typeface);
    typeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
    appHost->addTypeface("emoji", typeface);
  }
    
    
  auto contentScale = size.height / self.bounds.size.height;
  auto sizeChanged = appHost->updateScreen(width, height, contentScale);
  if (sizeChanged && window != nullptr) {
    window->invalidSize();
 }
    fWidth = width;
    fHeight = height;
 
  [self initRects: kTotalRectCount];
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [self updateSize];
}

- (void)draw:(int)index {
  if (!appHost) {
      return;
    }
  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return;
  }
  if (window == nullptr) {
    window = tgfx::CGLWindow::MakeFrom(self);
  }
  if (window == nullptr) {
    return;
  }
  auto device = window->getDevice();
    //if(!context)
    {
        context = device->lockContext();
    }
        
  if (context == nullptr) {
    return;
  }
  auto surface = window->getSurface(context);
  if (surface == nullptr) {
   // device->unlock();
    return;
  }
    
    
    auto canvas = surface->getCanvas();
    canvas->clear();
    canvas->save();
//    canvas->translate(0, 48 * appHost->density());
    
    if (startTime < 0) {
        startTime = GetCurrentTimestamp();
    }
    [self drawRects:canvas];
    [self drawFPS: canvas];
    canvas->restore();
      
      context->flushAndSubmit();
      window->present(context);
    
    device->unlock();
    [self animateRects];
    
}



- (NSString*)fpsInfo {
    return fFpsInfo;
}

@end
