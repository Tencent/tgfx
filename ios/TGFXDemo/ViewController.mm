//
//  ViewController.m
//  TGFXDemo
//
//  Created by lvpengwei on 2023/11/1.
//

#import "ViewController.h"
#include "tgfx/gpu/Surface.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/opengl/GLDevice.h"
#include "tgfx/opengl/eagl/EAGLWindow.h"

@interface TGFXView : UIView {
    int _width;
    int _height;
    std::shared_ptr<tgfx::EAGLWindow> window;
    std::shared_ptr<tgfx::Surface> surface;
    int drawCount;
}

- (void)draw;

@end

@implementation TGFXView

+ (Class)layerClass {
  return [CAEAGLLayer class];
}

- (void)setBounds:(CGRect)bounds {
    CGRect oldBounds = self.bounds;
    [super setBounds:bounds];
    if (oldBounds.size.width != bounds.size.width || oldBounds.size.height != bounds.size.height) {
      [self updateSize];
    }
}

- (void)setFrame:(CGRect)frame {
  CGRect oldRect = self.frame;
  [super setFrame:frame];
  if (oldRect.size.width != frame.size.width || oldRect.size.height != frame.size.height) {
    [self updateSize];
  }
}

- (void)setContentScaleFactor:(CGFloat)scaleFactor {
  CGFloat oldScaleFactor = self.contentScaleFactor;
  [super setContentScaleFactor:scaleFactor];
  if (oldScaleFactor != scaleFactor) {
    [self updateSize];
  }
}

- (void)updateSize {
    auto width = self.layer.bounds.size.width * self.layer.contentsScale;
    auto height = self.layer.bounds.size.height * self.layer.contentsScale;
    _width = static_cast<int>(roundf(width));
    _height = static_cast<int>(roundf(height));
    surface = nullptr;
}

- (void)createSurface {
    if (_width <= 0 || _height <= 0) {
        return;
    }
    if (window == nullptr) {
        window = tgfx::EAGLWindow::MakeFrom((CAEAGLLayer*)[self layer]);
    }
    if (window == nullptr) {
        return;
    }
    auto device = window->getDevice();
    auto context = device->lockContext();
    if (context == nullptr) {
        return;
    }
    surface = window->createSurface(context);
    device->unlock();
}

- (void)draw {
    if (surface == nullptr) {
        [self createSurface];
    }
    if (window == nullptr) {
        return;
    }
    auto device = window->getDevice();
    auto context = device->lockContext();
    if (context == nullptr) {
        return;
    }
    auto canvas = surface->getCanvas();
    canvas->clear();
    tgfx::Paint paint;
    paint.setColor(tgfx::Color{0.8f, 0.8f, 0.8f, 1.f});
    if (drawCount % 2 == 0) {
        auto rect = tgfx::Rect::MakeXYWH(20, 20, 100, 100);
        canvas->drawRect(rect, paint);
    } else {
        int tileSize = 8;
        for (int y = 0; y < _height; y += tileSize) {
            bool draw = (y / tileSize) % 2 == 1;
            for (int x = 0; x < _width; x += tileSize) {
                if (draw) {
                    auto rect = tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                                     static_cast<float>(tileSize), static_cast<float>(tileSize));
                    canvas->drawRect(rect, paint);
                }
                draw = !draw;
            }
        }
    }
    surface->flush();
    context->submit();
    window->present(context);
    device->unlock();
    drawCount++;
}

@end

@interface ViewController ()
@property (weak, nonatomic) IBOutlet TGFXView *tgfxView;
@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tgfxViewClicked)];
    [self.tgfxView addGestureRecognizer:tap];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self.tgfxView draw];
    });
}

- (void)tgfxViewClicked {
    [self.tgfxView draw];
}

@end
