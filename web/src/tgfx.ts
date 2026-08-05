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
import {getTGFXModule} from './tgfx-module';

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
    // Use "copy" composite operation to avoid source-over blending artifacts from canvas reuse.
    // This ensures the image pixels are written directly without compositing with any residual
    // background, which can cause white fringe on semi-transparent edges.
    ctx.globalCompositeOperation = 'copy';
    ctx.drawImage(image, 0, 0, width, height);
    ctx.globalCompositeOperation = 'source-over';
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

// Resolves the GPUDevice from an emscripten manager id without throwing. Manager.get() aborts
// (ASSERTIONS builds) or throws a TypeError (release builds) for unregistered ids, which would
// bypass the preinitializedWebGPUDevice fallback below. Accessing the internal objects map is the
// pattern emscripten's own libwebgpu.js uses (WebGPU.mgrDevice.objects[deviceId].object).
const resolveWebGPUDevice = (module: any, deviceId?: number): any => {
    const device = deviceId ? module?.WebGPU?.mgrDevice?.objects?.[deviceId]?.object : null;
    return device ?? module?.preinitializedWebGPUDevice;
};

// Configures the WebGPU canvas context to use the target color space. The emscripten surface
// configuration (wgpuSurfaceConfigure) does not expose the colorSpace option, so we re-run
// GPUCanvasContext.configure() here with the same parameters plus the desired color space.
// getContext('webgpu') is idempotent and returns the same context object already used by the
// emscripten WebGPU surface, so reconfiguring it only updates the color space.
// format, usage and alphaMode are passed in from the C++ side so that the WGPUSurfaceConfiguration
// stays the single source of truth for the canvas configuration.
export const configureWebGPUColorSpace = (
    canvasSelector: string,
    colorSpace: WindowColorSpace,
    deviceId?: number,
    format?: string,
    usage?: number,
    alphaMode?: string,
) => {
    if (colorSpace === WindowColorSpace.Others) {
        return false;
    }
    const canvas = document.querySelector(canvasSelector) as HTMLCanvasElement | null;
    if (!canvas) {
        return false;
    }
    const context = canvas.getContext('webgpu') as any;
    if (!context || typeof context.configure !== 'function') {
        return false;
    }
    // Resolve the GPUDevice the C++ side renders with. The C++ side passes its WGPUDevice handle so
    // this function can look up the same device object via the WebGPU runtime export
    // (Module.WebGPU, exported via EXPORTED_RUNTIME_METHODS), which also covers devices passed in
    // via WebGPUDevice::MakeFrom(). When the handle cannot be resolved, fall back to the module's
    // preinitializedWebGPUDevice for the default device path.
    const device = resolveWebGPUDevice(getTGFXModule(), deviceId);
    if (!device) {
        return false;
    }
    const gpuTextureUsage = (globalThis as any).GPUTextureUsage;
    const renderAttachment = gpuTextureUsage ? gpuTextureUsage.RENDER_ATTACHMENT : 0x10;
    context.configure({
        device: device,
        format: format ?? 'bgra8unorm',
        usage: usage ?? renderAttachment,
        alphaMode: alphaMode ?? 'premultiplied',
        colorSpace: colorSpace === WindowColorSpace.DisplayP3 ? 'display-p3' : 'srgb',
    });
    return true;
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

export const uploadVideoToWebGPUTexture = (source: HTMLVideoElement, texturePtr: number,
                                           width: number, height: number, deviceId?: number) => {
    if (!source || !texturePtr) {
        return;
    }
    syncVideoFrame(source);
    // Emscripten maps WGPUTexture C pointers to JS GPUTexture objects via WebGPU.mgrTexture,
    // exported on the module through EXPORTED_RUNTIME_METHODS.
    const module = getTGFXModule() as any;
    const WebGPU = module?.WebGPU;
    if (!WebGPU) {
        return;
    }
    const gpuTexture = WebGPU.mgrTexture?.objects?.[texturePtr]?.object;
    if (!gpuTexture) {
        return;
    }
    // Resolve the GPUDevice that owns the texture. The C++ side passes its WGPUDevice handle so the
    // queue used for upload matches the device the texture was created on. Fall back to the
    // module's preinitializedWebGPUDevice for the default device path.
    const device = resolveWebGPUDevice(module, deviceId);
    if (!device || !device.queue) {
        return;
    }
    device.queue.copyExternalImageToTexture(
        {source: source},
        {texture: gpuTexture, premultipliedAlpha: true},
        [width, height]
    );
};

export {getCanvas2D as createCanvas2D};
