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

import {TGFXModule} from '../tgfx-module';
import {ctor} from '../types';
import {getCanvas2D, releaseCanvas2D} from './canvas';

export class PathRasterizer {
    public static readPixels(width: number, height: number, path: Path2D, fillType: ctor): Uint8Array | null {
        let canvas = getCanvas2D(width, height);
        let context = canvas?.getContext('2d', {willReadFrequently: true}) as unknown as CanvasRenderingContext2D;
        if (!context) {
            return null;
        }
        context.setTransform(1, 0, 0, 1, 0, 0);
        if (fillType === TGFXModule.TGFXPathFillType.InverseWinding ||
            fillType === TGFXModule.TGFXPathFillType.InverseEvenOdd) {
            context.clip(path, fillType === TGFXModule.TGFXPathFillType.InverseEvenOdd ? 'evenodd' : 'nonzero');
            context.fillRect(0, 0, width, height);
        } else {
            context.fill(path, fillType === TGFXModule.TGFXPathFillType.EvenOdd ? 'evenodd' : 'nonzero');
        }
        const {data} = context.getImageData(0, 0, width, height);
        releaseCanvas2D(canvas);
        if (data.length === 0) {
            return null;
        }
        return new Uint8Array(data);
    }
}
