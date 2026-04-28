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

import { TGFX } from '../types';
import { setTGFXModule } from '../tgfx-module';
import * as tgfx from './tgfx';
import { Matrix } from '../core/matrix';
import { ScalerContext } from '../core/scaler-context';
import { PathRasterizer } from '../core/path-rasterizer';
import { setCanvasProvider } from '../core/canvas-provider';
import { getCanvas2D, releaseCanvas2D } from './canvas';

export const TGFXBind = (module: TGFX) => {
  setTGFXModule(module);
  module.module = module;
  // Inject the WeChat-specific canvas provider BEFORE exposing ScalerContext /
  // PathRasterizer on the module. The WASM layer may call into these classes
  // immediately after binding, and any canvas allocation done by the browser
  // default provider would fail in the mini-program environment (no document,
  // no HTMLCanvasElement). This ordering is a hard requirement.
  //
  // releaseCanvas2D is wrapped to bridge the signature gap: the WeChat pool only
  // accepts OffscreenCanvas, so we filter out any unexpected HTMLCanvasElement
  // instances instead of hiding the mismatch behind a type assertion.
  setCanvasProvider({
    getCanvas2D,
    releaseCanvas2D: (canvas: HTMLCanvasElement | OffscreenCanvas) => {
      if (canvas instanceof OffscreenCanvas) {
        releaseCanvas2D(canvas);
      }
    },
  });
  module.ScalerContext = ScalerContext;
  module.PathRasterizer = PathRasterizer;
  module.Matrix = Matrix;
  module.tgfx = { ...tgfx };
};
