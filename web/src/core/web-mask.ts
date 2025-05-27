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

import {TGFXModule} from '../tgfx-module';
import {ctor, Point, Vector} from '../types';
import {ScalerContext} from './scaler-context';
import {Matrix} from './matrix';
import {releaseCanvas2D} from '../utils/canvas';

export interface WebFont {
    name: string;
    style: string;
    size: number;
    bold: boolean;
    italic: boolean;
}

export class WebMask {
    public static create(canvas: HTMLCanvasElement | OffscreenCanvas) {
        return new WebMask(canvas);
    }

    protected canvas: HTMLCanvasElement | OffscreenCanvas;
    private context: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;

    public constructor(canvas: HTMLCanvasElement | OffscreenCanvas) {
        this.canvas = canvas;
        this.context = this.canvas.getContext('2d') as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
    }

    public updateCanvas(canvas: HTMLCanvasElement | OffscreenCanvas) {
        this.canvas = canvas;
    }

    public fillPath(path: Path2D, fillType: ctor) {
        this.context.setTransform(1, 0, 0, 1, 0, 0);
        if (
            fillType === TGFXModule.TGFXPathFillType.InverseWinding ||
            fillType === TGFXModule.TGFXPathFillType.InverseEvenOdd
        ) {
            this.context.clip(path, fillType === TGFXModule.TGFXPathFillType.InverseEvenOdd ? 'evenodd' : 'nonzero');
            this.context.fillRect(0, 0, this.canvas.width, this.canvas.height);
        } else {
            this.context.fill(path, fillType === TGFXModule.TGFXPathFillType.EvenOdd ? 'evenodd' : 'nonzero');
        }
    }

    public fillText(webFont: WebFont, texts: Vector<string>, positions: Vector<Point>, matrixWasmIns: any) {
        const scalerContext = new ScalerContext(webFont.name, webFont.style, webFont.size);
        const matrix = new Matrix(matrixWasmIns);
        this.context.setTransform(matrix.a, matrix.b, matrix.c, matrix.d, matrix.tx, matrix.ty);
        this.context.font = scalerContext.fontString(webFont.bold, webFont.italic);
        for (let i = 0; i < texts.size(); i++) {
            const position = positions.get(i);
            this.context.fillText(texts.get(i), position.x, position.y);
        }
    }

    public strokeText(
        webFont: WebFont,
        stroke: { width: number; cap: ctor; join: ctor; miterLimit: number },
        texts: Vector<string>,
        positions: Vector<Point>,
        matrixWasmIns: any,
    ) {
        if (stroke.width < 0.5) {
            return;
        }
        const scalerContext = new ScalerContext(webFont.name, webFont.style, webFont.size);
        const matrix = new Matrix(matrixWasmIns);
        this.context.setTransform(matrix.a, matrix.b, matrix.c, matrix.d, matrix.tx, matrix.ty);
        this.context.font = scalerContext.fontString(webFont.bold, webFont.italic);
        this.context.lineJoin = ScalerContext.getLineJoin(stroke.join);
        this.context.miterLimit = stroke.miterLimit;
        this.context.lineCap = ScalerContext.getLineCap(stroke.cap);
        this.context.lineWidth = stroke.width;
        for (let i = 0; i < texts.size(); i++) {
            const position = positions.get(i);
            this.context.strokeText(texts.get(i), position.x, position.y);
        }
    }

    public clear() {
        this.context.clearRect(0, 0, this.canvas.width, this.canvas.height);
    }

    public getImageData(){
        if (this.canvas == null){
            return null;
        }
        const context = this.canvas.getContext('2d',{willReadFrequently: true}) as CanvasRenderingContext2D;
        const {data} = context.getImageData(0, 0, this.canvas.width, this.canvas.height);
        releaseCanvas2D(this.canvas);
        if (data.length === 0) {
            return null;
        }
        return new Uint8Array(data);
    }
}
