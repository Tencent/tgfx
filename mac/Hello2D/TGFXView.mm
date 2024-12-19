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


constexpr int kTotalRectCount = 50000;
static int frames = 0;
static uint64_t startMs = 0;
bool fAnimateEnabled = true;
NSString* fFpsInfo = @"TGFX";
float fps = 0.f;
int curRectCount = 0; //当前矩形个数
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
    float size;
    float speed;
    tgfx::Rect rect;
};



std::deque<uint64_t> timestamps;

std::vector<RectData> fRects;

@implementation TGFXView {
  std::shared_ptr<tgfx::CGLWindow> window;
  std::unique_ptr<drawers::AppHost> appHost;
    tgfx::Context* context;
}

- (instancetype)initWithFrame:(NSRect)frameRect{
    
    context = nullptr;
    return [super initWithFrame:frameRect];
    
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
    //context = nullptr;
//   CGSize size = [self convertSizeToBacking:self.bounds.size];
//    auto width = static_cast<int>(roundf(size.width));
//    auto height = static_cast<int>(roundf(size.height));
    fRects.clear();
    
    std::mt19937 rng(18);
    std::mt19937 rngSpeed(36);
    std::uniform_real_distribution<float> disbution(0, 1);
    std::uniform_real_distribution<float> speedDistribution(1, 2);

    
    
    for (int i = 0; i < count; ++i) {
        float x = disbution(rng) * fWidth;
        float y = disbution(rng) * fHeight;
        float size = 10.0 + disbution(rng) * 40.0;
        float speed = 1.0 + speedDistribution(rngSpeed);
        tgfx::Rect rect = tgfx::Rect::MakeXYWH(x, y, size, size);

        fRects.push_back(RectData{ size, speed, rect });
    }
    
//    fRects.clear();
//    NSString* fname = appHost->density() > 1.5f ? @ "50000_dat_2" : @"50000_dat";
//    NSString* imagePath = [[NSBundle mainBundle] pathForResource:fname ofType:@"txt"];
//    std::string file_path = [imagePath UTF8String];
//    std::ifstream inputFile(file_path, std::ios::in);
//
//      // 检查文件是否成功打开
//      if (!inputFile.is_open()) {
//        std::cout << "无法打开文件！" << std::endl;
//        return ;
//      }
//
//      std::string line;
//      // 逐行读取文件
//      while (std::getline(inputFile, line)) {
//        std::istringstream iss(line); // 用于分割每行数据的字符串流
//        float x, y, size, speed;
//
//        // 从字符串流中读取四个数字
//        if (iss >> x >> y >> size >> speed) {
//            auto rect = tgfx::Rect::MakeXYWH(x, y, size, size);
//            fRects.push_back(RectData{ size, speed, rect });
//        } else {
//          std::cout << "无法从行 \"" << line << "\" 中读取四个数字." <<std::endl;
//        }
//          
     //}
    
}

- (void)drawRects: (tgfx::Canvas*) canvas{
    tgfx::Paint paint1;
    paint1.setColor(tgfx::Color::Red());
    //paint1.setStyle(tgfx::PaintStyle::Stroke);
    paint1.setAntiAlias(false);

    tgfx::Paint paint2;
    paint2.setColor(tgfx::Color::Green());
    //paint2.setStyle(tgfx::PaintStyle::Stroke);
    paint2.setAntiAlias(false);

    tgfx::Paint paint3;
    paint3.setColor(tgfx::Color::Blue());
    //paint3.setStyle(tgfx::PaintStyle::Stroke);
    paint3.setAntiAlias(false);

    int n = 0;
    for (int i = 0; i < curRectCount; ++i) {
        RectData* r = &fRects[i];

        int m = n++;
        if (m == 0)  canvas->drawRect(r->rect, paint1);
        else if (m == 1)  canvas->drawRect(r->rect, paint2);
        else if (m == 2)  canvas->drawRect(r->rect, paint3);

        if (n > 2) n = 0;
    }
}

- (void)drawFPS:(tgfx::Canvas*) canvas{
    tgfx::Paint paint;
    paint.setColor(tgfx::Color{0.32f,0.42f,0.62f,1.f});
    auto rect = tgfx::Rect::MakeWH(fWidth * 1.f, 48 * appHost->density());
    canvas->drawRect(rect, paint);
   
    //return;
    
    auto clr = tgfx::Color::White();
    if (fps > 58.f) {
        clr = tgfx::Color::Green();
    }else if (fps > 28.f){
        clr = tgfx::Color({1.f, 1.f, 0.f,1.f});//Yellow
        //clr = tgfx::Color({.92f, 0.73f, 0.48f,1.f});
    }
    else{
        clr = tgfx::Color({1.f, 0.f, 1.f,1.f});//ColorMAGENTA
        
        //clr = tgfx::Color({0.76f, 0.41f,0.38f,1.f});
    }
    
    tgfx::Paint text_paint;
    text_paint.setColor(clr);
    auto typeface = appHost->getTypeface("default");
    tgfx::Font font(typeface, 40 * appHost->density());
    //font.setFauxBold(true);
    std::string info = [fFpsInfo UTF8String];
    canvas->drawSimpleText(info, 110 * appHost->density(), 38 * appHost->density(), font, text_paint);
}

- (void) animateRects: (uint64_t) ms{
    //CGSize size = [self convertSizeToBacking:self.bounds.size];
    //auto width = static_cast<int>(roundf(size.width));
    for (int i = 0; i < curRectCount; ++i) {
        RectData* r = &fRects[i];
        float x = r->rect.x();

        x -= r->speed;
        if (x + r->size < 0) {
            x = fWidth + r->size;
        }

        r->rect.setXYWH(x, r->rect.y(), r->size, r->size);
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
    canvas->translate(0, 48 * appHost->density());
    [self drawRects:canvas];
    canvas->restore();
    //  auto numDrawers = drawers::Drawer::Count() - 1;
    //  index = (index % numDrawers) + 1;
    //  auto drawer = drawers::Drawer::GetByName("GridBackground");
    //  drawer->draw(canvas, appHost.get());
    //  drawer = drawers::Drawer::GetByIndex(index);
    //  drawer->draw(canvas, appHost.get());
    //  context->flushAndSubmit();
    //  window->present(context);
    //  device->unlock();
    [self drawFPS : canvas];
      
      
      context->flushAndSubmit();
      window->present(context);
    
    device->unlock();
    if(fAnimateEnabled)
    {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        auto ms = now_ms.time_since_epoch().count();
//        if (frames == 0)
//            startMs = ms;
//        ++frames;
//        if (ms - startMs < 3000) {
//            curRectCount = 20000;
//            return;
//        }else if(!clearWarmUp){
//            curRectCount = 0;
//            clearWarmUp = true;
//        }
        
//        if (frames <= 0)
//            startMs = ms;
//         ++frames;
//        if(curRectCount > 20000 && !clearWarmUp){
//            curRectCount = 0;
//            clearWarmUp = true;
//        }
        
        if (lastIncreaseMs == 0) {
            lastIncreaseMs = ms;
        }
        
        ++accFrames;
        if(!timestamps.empty()){
            while (ms - timestamps.front() > 1000) {
                timestamps.pop_front();
            }
            auto frame_count = timestamps.size();
            if (frame_count > 0 && accFrames == fpsFlushStep) {
            //if (frame_count > 0 && ms - lastIncreaseMs > fpsFlushSpan) {
                fps = frame_count * 1000.f / (ms - timestamps.front());
                fFpsInfo = [NSString stringWithFormat:@"Rectangles: %d, FPS:%.1f", curRectCount, fps];
                accFrames = 0;
                auto dur = ms - lastIncreaseMs;
                int accCount = static_cast<int>(1.f * dur / fpsFlushSpan * kIncreaseStep);
                curRectCount += accCount;
                //curRectCount += kIncreaseStep;
                curRectCount = std::min(curRectCount, (int)fRects.size());
                lastIncreaseMs = ms;
            }
        }
        timestamps.push_back(ms);
        
        
//        
//        if (ms - startMs > 1000) {
//            fps = frames * 1000.f / (ms - startMs);
//            
//            fFpsInfo = [NSString stringWithFormat:@"Count:%d\tFPS:%.2f", curRectCount, fps];
//            //NSLog(@"%@",fFpsInfo);]
//            frames = 0;
//            startMs = ms;
//        }
        
//        if (lastIncreaseMs == 0) {
//            lastIncreaseMs = ms;
//        }
//        
//        if (ms - lastIncreaseMs > kIncreaseSpan) {
//            curRectCount += kIncreaseStep;
//            curRectCount = std::min(curRectCount, (int)fRects.size());
//            lastIncreaseMs = ms;
//        }
       
        
        [self animateRects : ms];
        
    }
    
}



- (NSString*)fpsInfo {
    return fFpsInfo;
}

@end
