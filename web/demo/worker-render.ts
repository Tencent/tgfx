/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Runs the whole tgfx pipeline off the main thread.
//
// The page calls HTMLCanvasElement.transferControlToOffscreen() and posts the result here, so this
// worker owns the display target. Frames are presented by the browser straight out of this thread:
// there is no hand-off back to the main thread, no transferToImageBitmap() and no explicit present
// call. Everything the settings panel would report is mirrored back with postMessage().
import {TGFXBind} from '../lib/tgfx';

// Resolved at runtime against this worker's own URL so that each build directory loads the module
// built for it. A static import is resolved at build time, which made the pthreads page run the
// single-threaded module.
const hello2dUrl = new URL('./hello2d.js', import.meta.url).href;

let module: any = null;
let view: any = null;
let canvas: OffscreenCanvas = null;
let density = 1;
let rendering = false;

// Instrumentation. A resize forces a drawing buffer reallocation, a Surface rebuild and a full tile
// re-raster, all on this same thread, so it competes with the render loop. These numbers make that
// visible instead of leaving it to guesswork.
let framesInWindow = 0;
let lastFrameAt = 0;
let maxFrameInterval = 0;
let maxFrameIntervalEver = 0;
let lastApplyMs = 0;
let maxApplyMs = 0;
let applyCount = 0;

const send = (type: string, payload?: Record<string, unknown>) => {
    self.postMessage({source: 'worker', type, ...(payload || {})});
};

self.onmessage = async (event: MessageEvent) => {
    const message = event.data;
    try {
        switch (message.type) {
            case 'init':
                await onInit(message);
                break;
            case 'resize':
                onResize(message);
                break;
            case 'zoom':
                view?.updateZoomScaleAndOffset(message.zoom, message.offsetX, message.offsetY);
                break;
            case 'layerTree':
                view?.updateLayerTree(message.drawIndex);
                break;
            case 'config':
                if (message.resizeIntervalMs !== undefined) {
                    resizeApplyIntervalMs = message.resizeIntervalMs;
                }
                if (message.resetStats) {
                    // Lets a drag be measured on its own instead of against the startup cost.
                    maxFrameIntervalEver = 0;
                    maxApplyMs = 0;
                    lastFrameAt = 0;
                }
                break;
            default:
                break;
        }
    } catch (error) {
        const where = message && message.type ? message.type + ': ' : '';
        send('error', {message: where + String(error && (error as Error).message || error)});
    }
};

async function onInit(message: any) {
    canvas = message.canvas;
    density = message.dpr || 1;
    setBackingSize(message.cssWidth, message.cssHeight, density);

    const Hello2D = (await import(hello2dUrl)).default;
    module = await Hello2D({
        // The wasm binary sits next to this worker script, and a module worker resolves relative
        // URLs against its own script URL.
        locateFile: (file: string) => './' + file,
    });
    TGFXBind(module);

    // TGFXView rather than TGFXThreadsView: the latter registers a typeface from raw font bytes,
    // which only works when the build enables FreeType, and the non-pthread web build has it off
    // (CMakeLists.txt turns FreeType on only when TGFX_USE_THREADS is set). TGFXView resolves the
    // typeface by family name through WebTypeface, which rasterizes glyphs with the browser, so it
    // works here as long as the families are registered in this worker's FontFaceSet.
    // The transferred canvas is handed straight to tgfx. The id based overload would resolve the id
    // through the Emscripten canvas lookup, which falls back to document.querySelector(), and a
    // worker has no document.
    view = module.TGFXView.MakeFromCanvas(canvas);
    if (!view) {
        send('error', {message: 'TGFXView.MakeFrom() returned null'});
        return;
    }
    if (message.useWebGPU) {
        // The device has to be created on this thread: a GPUDevice cannot be transferred from the
        // page, and a worker has no default device to fall back on. It has to be supplied before
        // updateSize() builds the window.
        view.setWebGPUDevice(await createWebGPUDevice());
    }

    for (const image of message.images) {
        view.setImagePath(image.name, image.bitmap);
    }
    // A worker has no document, so the browser side font registry has to be filled in explicitly
    // before the view asks tgfx for a typeface.
    await registerWorkerFonts(message.fonts);
    view.registerFonts();

    // A worker cannot read the layout size, so the density is pushed in instead of queried.
    view.setLayoutDensity(density);
    view.updateSize();
    view.updateLayerTree(0);
    view.updateZoomScaleAndOffset(1.0, 0, 0);

    resizeApplyIntervalMs = message.resizeIntervalMs || 0;
    send('ready', {width: canvas.width, height: canvas.height, density});
    startRendering();
    setInterval(() => {
        send('stats', {
            fps: framesInWindow * 2,
            maxFrameInterval: maxFrameInterval,
            maxFrameIntervalEver: maxFrameIntervalEver,
            lastApplyMs: lastApplyMs,
            maxApplyMs: maxApplyMs,
            applyCount: applyCount,
        });
        framesInWindow = 0;
        maxFrameInterval = 0;
    }, 500);
}

// Only the thread that owns a transferred OffscreenCanvas may resize it, so the main thread reports
// the layout size and this side owns the backing store size.
function setBackingSize(cssWidth: number, cssHeight: number, dpr: number) {
    canvas.width = Math.max(1, Math.round(cssWidth * dpr));
    canvas.height = Math.max(1, Math.round(cssHeight * dpr));
}

// Applying a resize means resizing the drawing buffer (which clears it), recreating the Surface and
// invalidating the display list's tiles. On a heavy tree that costs far more than a frame, so a drag
// that applies every step can never settle. A non-zero interval here switches to a leading plus
// trailing throttle: a lone resize still applies immediately, a continuous drag is limited to one
// apply per interval. Zero means apply every step.
let resizeApplyIntervalMs = 0;
let lastResizeAppliedAt = 0;
let pendingResize: {cssWidth: number; cssHeight: number; dpr: number} | null = null;

// Only records what to do. The work happens in the frame callback below, so a burst of resize
// messages can never pile up and starve the render loop -- which is what made the canvas sit on one
// frame and then jump once the drag ended.
function onResize(message: any) {
    if (!canvas) {
        // A resize that raced ahead of init. init carries the layout that is current at that point,
        // so dropping this one is safe; the page also stops sending them until init is posted.
        return;
    }
    density = message.dpr || 1;
    pendingResize = {cssWidth: message.cssWidth, cssHeight: message.cssHeight, dpr: density};
    if (!view) {
        // Still loading: keep the backing store in step, there is nothing to redraw yet.
        setBackingSize(pendingResize.cssWidth, pendingResize.cssHeight, density);
        pendingResize = null;
    }
}

// Applied from the frame callback, at most once per frame, reusing the draw that is about to happen.
// Resizing the drawing buffer clears it, so drawing immediately afterwards also keeps a cleared
// frame from reaching the compositor.
function applyPendingResize() {
    if (!pendingResize || !view) {
        return;
    }
    const sinceLastApply = performance.now() - lastResizeAppliedAt;
    if (sinceLastApply < resizeApplyIntervalMs) {
        return;
    }
    const {cssWidth, cssHeight, dpr} = pendingResize;
    pendingResize = null;
    lastResizeAppliedAt = performance.now();
    const startedAt = performance.now();
    setBackingSize(cssWidth, cssHeight, dpr);
    view.setLayoutDensity(dpr);
    // Re-creates the Surface so the draw below picks up the new size.
    view.updateSize();
    lastApplyMs = performance.now() - startedAt;
    if (lastApplyMs > maxApplyMs) {
        maxApplyMs = lastApplyMs;
    }
    applyCount++;
    send('resized', {width: canvas.width, height: canvas.height});
}

function startRendering() {
    if (rendering) {
        return;
    }
    rendering = true;
    const frame = (now: number) => {
        if (lastFrameAt) {
            const interval = now - lastFrameAt;
            if (interval > maxFrameInterval) {
                maxFrameInterval = interval;
            }
            if (interval > maxFrameIntervalEver) {
                maxFrameIntervalEver = interval;
            }
        }
        lastFrameAt = now;
        framesInWindow++;
        applyPendingResize();
        view.draw();
        requestAnimationFrame(frame);
    };
    // A dedicated worker has requestAnimationFrame and it is aligned to the same document frame as
    // the main thread, so the cadence stays in step with the display.
    requestAnimationFrame(frame);
}

// Registers the font bytes in this worker's FontFaceSet. WebTypeface builds its CSS font string from
// the family name ("default" / "emoji") and lets the browser rasterize the glyphs, so the families
// have to exist on this thread. FontFace and self.fonts are both available in a dedicated worker.
async function registerWorkerFonts(fonts: {default: ArrayBuffer; emoji: ArrayBuffer}) {
    const workerFonts = (self as unknown as {fonts: FontFaceSet}).fonts;
    const faces: Array<[string, ArrayBuffer]> = [['default', fonts.default], ['emoji', fonts.emoji]];
    for (const [family, bytes] of faces) {
        const face = new FontFace(family, bytes);
        await face.load();
        workerFonts.add(face);
    }
}

// A GPUDevice cannot be moved across a worker boundary, so it is created on this thread instead of
// being handed over by the page. navigator.gpu is available in a dedicated worker.
async function createWebGPUDevice(): Promise<any> {
    const gpu = (navigator as unknown as {gpu?: any}).gpu;
    if (!gpu) {
        throw new Error('WebGPU is not supported in this browser.');
    }
    const adapter = await gpu.requestAdapter();
    if (!adapter) {
        throw new Error('Failed to get the WebGPU adapter.');
    }
    const device = await adapter.requestDevice();
    if (!device) {
        throw new Error('Failed to get the WebGPU device.');
    }
    return device;
}
