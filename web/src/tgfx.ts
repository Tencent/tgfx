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

import {getCanvas2D, isCanvas, releaseCanvas2D} from './utils/canvas';
import {BitmapImage} from './core/bitmap-image';
import {isInstanceOf} from './utils/type-utils';

import {EmscriptenGL, TGFX, WindowColorSpace} from './types';
import type {wx} from './wechat/interfaces';

declare const wx: wx;

export const createImage = (source: string) => {
    return new Promise<HTMLImageElement | null>((resolve) => {
        const image = new Image();
        image.onload = function () {
            resolve(image);
        };
        image.onerror = function () {
            console.error('image create from bytes error.');
            resolve(null);
        };
        image.src = source;
    });
};

export const createImageFromBytes = (bytes: ArrayBuffer) => {
    const uint8Array = new Uint8Array(bytes);
    const blob = new Blob([uint8Array], {type: 'image/*'});
    return createImage(URL.createObjectURL(blob));
};

export const readImagePixels = (module: TGFX, image: CanvasImageSource, width: number, height: number) => {
    if (!image) {
        return null;
    }
    const canvas = getCanvas2D(width, height);
    const ctx = canvas.getContext('2d', {willReadFrequently: true}) as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D | null;
    if (!ctx) {
        return null;
    }
    ctx.drawImage(image, 0, 0, width, height);
    const {data} = ctx.getImageData(0, 0, width, height);
    releaseCanvas2D(canvas);
    if (data.length === 0) {
        return null;
    }
    return new Uint8Array(data);
};

export const hasWebpSupport = () => {
    try {
        return document.createElement('canvas').toDataURL('image/webp', 0.5).indexOf('data:image/webp') === 0;
    } catch (err) {
        return false;
    }
};

export const getSourceSize = (source: TexImageSource | OffscreenCanvas) => {
    if (isInstanceOf(source, globalThis.HTMLVideoElement)) {
        return {
            width: (source as HTMLVideoElement).videoWidth,
            height: (source as HTMLVideoElement).videoHeight,
        };
    }
    return {width: source.width, height: source.height};
};

// Workaround: Frame-by-frame video playback shows duplicate frames on Web.
// Setting video.currentTime is async - the browser decodes and composites the frame in background.
// Calling texSubImage2D immediately after setting currentTime may read stale frame data.
// Drawing video to canvas forces the browser to composite the current frame synchronously.
// Note: requestVideoFrameCallback is not used because it's async, requires architecture changes,
// and has limited browser support (Chrome 83+, Safari 15.4+).
let syncCanvas: HTMLCanvasElement | null = null;
let syncCtx: CanvasRenderingContext2D | null = null;

const syncVideoFrame = (video: HTMLVideoElement): void => {
    if (!syncCanvas) {
        syncCanvas = document.createElement('canvas');
        syncCanvas.width = 1;
        syncCanvas.height = 1;
        syncCtx = syncCanvas.getContext('2d');
        if (!syncCtx) {
            syncCanvas = null;
            return;
        }
    }
    if (!syncCtx) {
        syncCanvas = null;
        return;
    }
    syncCtx.drawImage(video, 0, 0, 1, 1);
};

export const uploadToTexture = (
    GL: EmscriptenGL,
    source: TexImageSource | OffscreenCanvas | BitmapImage,
    textureID: number,
    offsetX: number,
    offsetY: number,
    alphaOnly: boolean,
) => {
    let renderSource = source instanceof BitmapImage ? source.bitmap : source;
    if (!renderSource) return;
    if (isInstanceOf(renderSource, globalThis.HTMLVideoElement)) {
        syncVideoFrame(renderSource as HTMLVideoElement);
    }
    const gl = GL.currentContext?.GLctx as WebGL2RenderingContext;
    gl.bindTexture(gl.TEXTURE_2D, GL.textures[textureID]);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
    if (alphaOnly) {
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, offsetX, offsetY, gl.RED, gl.UNSIGNED_BYTE, renderSource);
    } else {
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, offsetX, offsetY, gl.RGBA, gl.UNSIGNED_BYTE, renderSource);
    }
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
};

export const setColorSpace = (
    GL: EmscriptenGL,
    colorSpace: WindowColorSpace
) => {
    if (colorSpace === WindowColorSpace.Others) {
        return false;
    }
    const gl = GL.currentContext?.GLctx as WebGLRenderingContext;
    if ('drawingBufferColorSpace' in gl) {
        if (colorSpace === WindowColorSpace.None || colorSpace === WindowColorSpace.SRGB) {
            gl.drawingBufferColorSpace = "srgb";
        } else {
            gl.drawingBufferColorSpace = "display-p3";
        }
        return true;
    } else if (colorSpace === WindowColorSpace.DisplayP3) {
        return false;
    } else {
        return true;
    }
};

export const isAndroidMiniprogram = () => {
    if (typeof wx !== 'undefined' && wx.getSystemInfoSync) {
        return wx.getSystemInfoSync().platform === 'android';
    }
};

export const releaseNativeImage = (source: TexImageSource | OffscreenCanvas) => {
    if (isInstanceOf(source, globalThis.ImageBitmap)) {
        (source as ImageBitmap).close();
    } else if (isCanvas(source)) {
        releaseCanvas2D(source as OffscreenCanvas | HTMLCanvasElement);
    }
};

export const getBytesFromPath = async (module: TGFX, path: string) => {
    const buffer = await fetch(path).then((res) => res.arrayBuffer());
    return new Uint8Array(buffer);
};

export {getCanvas2D as createCanvas2D};
