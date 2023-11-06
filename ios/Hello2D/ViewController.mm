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

@interface ViewController ()
@property(weak, nonatomic) IBOutlet TGFXView* tgfxView;
@end

@implementation ViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tgfxView.contentScaleFactor = [UIScreen mainScreen].scale;
  UITapGestureRecognizer* tap =
      [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tgfxViewClicked)];
  [self.tgfxView addGestureRecognizer:tap];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   [self.tgfxView draw];
                 });
}

- (void)tgfxViewClicked {
  [self.tgfxView draw];
}

@end
