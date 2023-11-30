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

import { TGFX } from '../types';
import { setTGFXModule } from '../tgfx-module';
import * as tgfx from './tgfx';
import { Matrix } from '../core/matrix';
import { ScalerContext } from '../core/scaler-context';
import { WebMask } from '../core/web-mask';

export const TGFXBind = (module: TGFX) => {
  setTGFXModule(module);
  module.module = module;
  module.ScalerContext = ScalerContext;
  module.WebMask = WebMask;
  module.Matrix = Matrix;
  module.tgfx = { ...tgfx };
};
