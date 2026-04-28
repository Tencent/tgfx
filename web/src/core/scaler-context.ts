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
import {getCanvasProvider} from './canvas-provider';
import {TGFXModule} from '../tgfx-module';
import {ctor, Rect} from '../types';

export class ScalerContext {
    // Shared lazily-initialized canvas/context slots. loadCanvas() is invoked
    // from every constructor, so any instance method reaching these fields
    // will see valid values. Declared as non-optional because the strict TS
    // mode forbids definite-assignment assertions on static fields; callers
    // must not touch these before an instance has been constructed.
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
            // Probe Unicode property escape support; the RegExp constructor
            // throws on engines that do not implement the \p{} syntax.
            // eslint-disable-next-line prefer-regex-literals
            new RegExp("\\p{L}", "u");
            return true;
        } catch {
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

    // Cache of the font-specific bounding box probed lazily in the fallback
    // measureText path. Since fontName is readonly on each instance, this is
    // effectively a one-slot memoization and does not need a Map keyed by name.
    private fontMeasureCache: Rect | undefined = undefined;

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
        const height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) {
            return null;
        }
        const provider = getCanvasProvider();
        const canvas = provider.getCanvas2D(width, height);
        const context = canvas.getContext('2d', {willReadFrequently: true}) as
            | CanvasRenderingContext2D
            | OffscreenCanvasRenderingContext2D
            | null;
        if (!context) {
            provider.releaseCanvas2D(canvas);
            return null;
        }
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
        provider.releaseCanvas2D(canvas);
        return new Uint8Array(data);
    }

    public getGlyphCanvas(
        text: string,
        bounds: Rect,
        fauxBold: boolean,
        stroke ?: { width: number; cap: ctor; join: ctor; miterLimit: number },
        padding: number = 0
    ): HTMLCanvasElement | OffscreenCanvas | null {
        const glyphWidth = bounds.right - bounds.left;
        const glyphHeight = bounds.bottom - bounds.top;
        const width = glyphWidth + 2 * padding;
        const height = glyphHeight + 2 * padding;
        // Guard on the final canvas size rather than the glyph-only size so
        // that a negative padding does not produce an invalid canvas request.
        if (width <= 0 || height <= 0) {
            return null;
        }
        const provider = getCanvasProvider();
        const canvas = provider.getCanvas2D(width, height);
        const context = canvas.getContext('2d') as
            | CanvasRenderingContext2D
            | OffscreenCanvasRenderingContext2D
            | null;
        if (!context) {
            // Return the unusable canvas back to the pool to avoid leaking it.
            provider.releaseCanvas2D(canvas);
            return null;
        }
        context.clearRect(0, 0, width, height);
        context.font = this.fontString(fauxBold, false);
        if (stroke) {
            context.strokeStyle = "#FFFFFF";
            context.lineJoin = ScalerContext.getLineJoin(stroke.join);
            context.miterLimit = stroke.miterLimit;
            context.lineCap = ScalerContext.getLineCap(stroke.cap);
            context.lineWidth = stroke.width;
            context.strokeText(text, -bounds.left + padding, -bounds.top + padding);
        } else {
            context.fillStyle = "#FFFFFF";
            context.fillText(text, -bounds.left + padding, -bounds.top + padding);
        }
        return canvas;
    }

    /**
     * Lazily initializes the shared canvas and 2D context used for font
     * metrics probing. Called from the constructor, so the first
     * `new ScalerContext(...)` in an environment without a working 2D context
     * will throw and propagate to the WASM bridge. Callers that embed
     * ScalerContext in a host without JS exception handling should ensure the
     * canvas provider is correctly injected before any instance is created.
     */
    protected loadCanvas() {
        if (!ScalerContext.canvas) {
            const canvas = getCanvasProvider().getCanvas2D(10, 10);
            ScalerContext.setCanvas(canvas);
            // https://html.spec.whatwg.org/multipage/canvas.html#concept-canvas-will-read-frequently
            const context = canvas.getContext('2d', {willReadFrequently: true}) as
                | CanvasRenderingContext2D
                | OffscreenCanvasRenderingContext2D
                | null;
            if (!context) {
                throw new Error('[tgfx] Failed to acquire a 2D context for the shared ScalerContext canvas.');
            }
            ScalerContext.setContext(context);
        }
    }

    private measureText(ctx: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D, text: string): TextMetrics {
        if (ScalerContext.measureDirectly(ctx)) {
            return ctx.measureText(text);
        }
        // Fallback path: the engine does not expose actualBoundingBox* metrics,
        // so we render into a dedicated temp canvas and recover the bounds from
        // pixel data. A temp canvas avoids mutating the shared ScalerContext
        // canvas, which would otherwise corrupt concurrent measurements and
        // incur a resize on every call. The canvas side is ceiled to an
        // integer because both canvas.width/height and getImageData(sw, sh)
        // require integer arguments.
        const side = Math.ceil(this.size * 1.5);
        const provider = getCanvasProvider();
        const tmpCanvas = provider.getCanvas2D(side, side);
        const tmpCtx = tmpCanvas.getContext('2d', {willReadFrequently: true}) as
            | CanvasRenderingContext2D
            | OffscreenCanvasRenderingContext2D
            | null;
        if (!tmpCtx) {
            provider.releaseCanvas2D(tmpCanvas);
            // ctx.measureText was already known not to return valid bounds
            // (that is why we took the fallback path). Return a zero-sized
            // metrics object instead so that upstream collapse checks in
            // getBounds / getFontMetrics see empty geometry rather than
            // silently using stale or bogus values.
            return {
                actualBoundingBoxAscent: 0,
                actualBoundingBoxRight: 0,
                actualBoundingBoxDescent: 0,
                actualBoundingBoxLeft: 0,
                fontBoundingBoxAscent: 0,
                fontBoundingBoxDescent: 0,
                width: 0,
            };
        }
        tmpCtx.font = ctx.font;
        // Baseline origin: drawing starts at x=0, with y=size so the glyph
        // can extend both upwards (ascent) and downwards (descent) inside
        // the temp canvas without clipping.
        const baselineY = this.size;
        tmpCtx.clearRect(0, 0, side, side);
        tmpCtx.fillText(text, 0, baselineY);
        const imageData = tmpCtx.getImageData(0, 0, side, side);
        const {left, top, right, bottom} = measureText(imageData);

        let fontMeasure = this.fontMeasureCache;
        if (!fontMeasure) {
            tmpCtx.clearRect(0, 0, side, side);
            tmpCtx.fillText('测', 0, baselineY);
            const fontImageData = tmpCtx.getImageData(0, 0, side, side);
            fontMeasure = measureText(fontImageData);
            this.fontMeasureCache = fontMeasure;
        }
        provider.releaseCanvas2D(tmpCanvas);

        return {
            actualBoundingBoxAscent: baselineY - top,
            actualBoundingBoxRight: right,
            actualBoundingBoxDescent: bottom - baselineY,
            actualBoundingBoxLeft: -left,
            fontBoundingBoxAscent: fontMeasure.bottom - fontMeasure.top,
            fontBoundingBoxDescent: 0,
            width: fontMeasure.right - fontMeasure.left,
        };
    }

}
