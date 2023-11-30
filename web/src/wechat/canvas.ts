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

import { CANVAS_POOL_MAX_SIZE } from '../constant';

import type { wx } from './interfaces';

declare const wx: wx;

const canvasPool = new Array<OffscreenCanvas>();

export const getCanvas2D = (width: number, height: number) => {
  let canvas = canvasPool.pop() || createCanvas2D();
  if (canvas !== null) {
    canvas.width = width;
    canvas.height = height;
  }
  return canvas;
};

export const releaseCanvas2D = (canvas: OffscreenCanvas) => {
  if (canvasPool.length < CANVAS_POOL_MAX_SIZE) {
    canvasPool.push(canvas);
  }
};

const createCanvas2D = () => {
  return wx.createOffscreenCanvas({ type: '2d' });
};
