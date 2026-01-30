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

import {measureText} from '../utils/measure-text';
import {defaultFontNames, getFontFamilies} from '../utils/font-family';
import {getCanvas2D, releaseCanvas2D} from '../utils/canvas';
import {TGFXModule} from '../tgfx-module';
import {ctor, Rect} from '../types';

export class ScalerContext {
    public static canvas: HTMLCanvasElement | OffscreenCanvas;
    public static context: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
    private static hasMeasureBoundsAPI: boolean | undefined = undefined;

    public static getLineCap(cap: ctor): CanvasLineCap {
        switch (cap) {
            case TGFXModule.TGFXLineCap.Round:
                return 'round';
            case TGFXModule.TGFXLineCap.Square:
                return 'square';
            default:
                return 'butt';
        }
    }

    public static getLineJoin(join: ctor): CanvasLineJoin {
        switch (join) {
            case TGFXModule.TGFXLineJoin.Round:
                return 'round';
            case TGFXModule.TGFXLineJoin.Bevel:
                return 'bevel';
            default:
                return 'miter';
        }
    }

    public static setCanvas(canvas: HTMLCanvasElement | OffscreenCanvas) {
        ScalerContext.canvas = canvas;
    }

    public static setContext(context: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D) {
        ScalerContext.context = context;
    }

    public static isUnicodePropertyEscapeSupported(): boolean {
        try {
            // eslint-disable-next-line prefer-regex-literals
            const regex = new RegExp("\\p{L}", "u");
            return true;
        } catch (e) {
            return false;
        }
    }

    public static isEmoji(text: string): boolean {
        let emojiRegExp: RegExp;
        if (this.isUnicodePropertyEscapeSupported()) {
            emojiRegExp = /\p{Extended_Pictographic}|[#*0-9]\uFE0F?\u20E3|[\uD83C\uDDE6-\uD83C\uDDFF]/u;
        } else {
            emojiRegExp = /(\u00a9|\u00ae|[\u2000-\u3300]|\ud83c[\ud000-\udfff]|\ud83d[\ud000-\udfff]|\ud83e[\ud000-\udfff])/;
        }
        return emojiRegExp.test(text);
    }

    private static measureDirectly(ctx: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D): boolean {
        if (ScalerContext.hasMeasureBoundsAPI === undefined) {
            const metrics = ctx.measureText('x');
            ScalerContext.hasMeasureBoundsAPI = metrics && metrics.actualBoundingBoxAscent > 0;
        }
        return ScalerContext.hasMeasureBoundsAPI;
    }

    private readonly fontName: string;
    private readonly fontStyle: string;
    private readonly size: number;
    private fontMetrics!: {
        ascent: number;
        descent: number;
        xHeight: number;
        capHeight: number;
    };

    private fontBoundingBoxMap: { key: string; value: Rect }[] = [];

    public constructor(fontName: string, fontStyle: string, size: number) {
        this.fontName = fontName;
        this.fontStyle = fontStyle;
        this.size = size;
        this.loadCanvas();
    }

    public fontString(fauxBold: boolean, fauxItalic: boolean) {
        const attributes = [];
        // css font-style
        if (fauxItalic) {
            attributes.push('italic');
        }
        // css font-weight
        if (fauxBold) {
            attributes.push('bold');
        }
        // css font-size
        attributes.push(`${this.size}px`);
        // css font-family
        const fallbackFontNames = defaultFontNames.concat();
        fallbackFontNames.unshift(...getFontFamilies(this.fontName, this.fontStyle));
        attributes.push(`${fallbackFontNames.join(',')}`);
        return attributes.join(' ');
    }

    public getFontMetrics() {
        if (this.fontMetrics) {
            return this.fontMetrics;
        }
        const {context} = ScalerContext;
        context.font = this.fontString(false, false);
        const metrics = this.measureText(context, 'H');
        const capHeight = metrics.actualBoundingBoxAscent;
        const xMetrics = this.measureText(context, 'x');
        const xHeight = xMetrics.actualBoundingBoxAscent;
        this.fontMetrics = {
            ascent: -metrics.fontBoundingBoxAscent,
            descent: metrics.fontBoundingBoxDescent,
            xHeight,
            capHeight,
        };
        return this.fontMetrics;
    }

    public getBounds(text: string, fauxBold: boolean, fauxItalic: boolean) {
        const {context} = ScalerContext;
        context.font = this.fontString(fauxBold, fauxItalic);
        const metrics = this.measureText(context, text);
        const bounds: Rect = {
            left: Math.floor(-metrics.actualBoundingBoxLeft),
            top: Math.floor(-metrics.actualBoundingBoxAscent),
            right: Math.ceil(metrics.actualBoundingBoxRight),
            bottom: Math.ceil(metrics.actualBoundingBoxDescent),
        };
        if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
            bounds.left = 0;
            bounds.top = 0;
            bounds.right = 0;
            bounds.bottom = 0;
        }
        return bounds;
    }

    public getAdvance(text: string) {
        const {context} = ScalerContext;
        context.font = this.fontString(false, false);
        return context.measureText(text).width;
    }

    public readPixels(
        text: string,
        bounds: Rect,
        fauxBold: boolean,
        stroke ?: { width: number; cap: ctor; join: ctor; miterLimit: number }
    ) {
        const width = bounds.right - bounds.left;
        const height = bounds.bottom - bounds.top
        const canvas = getCanvas2D(width, height);
        const context = canvas.getContext('2d',{willReadFrequently: true}) as CanvasRenderingContext2D;
        context.clearRect(0, 0, width, height);
        context.font = this.fontString(fauxBold, false);
        if (stroke){
            context.lineJoin = ScalerContext.getLineJoin(stroke.join);
            context.miterLimit = stroke.miterLimit;
            context.lineCap = ScalerContext.getLineCap(stroke.cap);
            context.lineWidth = stroke.width;
            context.strokeText(text, -bounds.left, -bounds.top);
        } else {
            context.fillText(text, -bounds.left, -bounds.top);
        }
        const {data} = context.getImageData(0, 0, width, height);
        releaseCanvas2D(canvas);
        if (data.length === 0) {
            return null;
        }
        return new Uint8Array(data);
    }

    public getGlyphCanvas(
        text: string,
        bounds: Rect,
        fauxBold: boolean,
        stroke ?: { width: number; cap: ctor; join: ctor; miterLimit: number }
    ): HTMLCanvasElement | OffscreenCanvas | null {
        const glyphWidth = bounds.right - bounds.left;
        const glyphHeight = bounds.bottom - bounds.top;
        if (glyphWidth <= 0 || glyphHeight <= 0) {
            return null;
        }
        const canvas = new OffscreenCanvas(glyphWidth, glyphHeight);
        const context = canvas.getContext('2d') as OffscreenCanvasRenderingContext2D;
        context.clearRect(0, 0, glyphWidth, glyphHeight);
        context.font = this.fontString(fauxBold, false);
        if (stroke) {
            context.lineJoin = ScalerContext.getLineJoin(stroke.join);
            context.miterLimit = stroke.miterLimit;
            context.lineCap = ScalerContext.getLineCap(stroke.cap);
            context.lineWidth = stroke.width;
            context.strokeText(text, -bounds.left, -bounds.top);
        } else {
            context.fillText(text, -bounds.left, -bounds.top);
        }
        return canvas;
    }

    protected loadCanvas() {
        if (!ScalerContext.canvas) {
            ScalerContext.setCanvas(getCanvas2D(10, 10));
            // https://html.spec.whatwg.org/multipage/canvas.html#concept-canvas-will-read-frequently
            ScalerContext.setContext(
                (ScalerContext.canvas as HTMLCanvasElement | OffscreenCanvas).getContext('2d', {willReadFrequently: true}) as
                    | CanvasRenderingContext2D
                    | OffscreenCanvasRenderingContext2D,
            );
        }
    }

    private measureText(ctx: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D, text: string): TextMetrics {
        if (ScalerContext.measureDirectly(ctx)) {
            return ctx.measureText(text);
        }
        ctx.canvas.width = this.size * 1.5;
        ctx.canvas.height = this.size * 1.5;
        const pos = [0, this.size];
        ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        ctx.fillText(text, pos[0], pos[1]);
        const imageData = ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height);
        const {left, top, right, bottom} = measureText(imageData);
        ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);

        let fontMeasure: Rect;
        const fontBoundingBox = this.fontBoundingBoxMap.find((item) => item.key === this.fontName);
        if (fontBoundingBox) {
            fontMeasure = fontBoundingBox.value;
        } else {
            ctx.fillText('测', pos[0], pos[1]);
            const fontImageData = ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height);
            fontMeasure = measureText(fontImageData);
            this.fontBoundingBoxMap.push({key: this.fontName, value: fontMeasure});
            ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        }

        return {
            actualBoundingBoxAscent: pos[1] - top,
            actualBoundingBoxRight: right - pos[0],
            actualBoundingBoxDescent: bottom - pos[1],
            actualBoundingBoxLeft: pos[0] - left,
            fontBoundingBoxAscent: fontMeasure.bottom - fontMeasure.top,
            fontBoundingBoxDescent: 0,
            width: fontMeasure.right - fontMeasure.left,
        };
    }

}
