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

// Page side of the asynchronous rendering demo. The main thread does not render at all: it hands the
// canvas to a worker, forwards input events as messages, and reports layout changes. Everything that
// used to be a direct call into tgfx goes through the worker instead.
import {ShareData, loadImage, onClickEvent, bindCanvasZoomAndPanEvents} from './common';

const canvas = document.getElementById('hello2d') as HTMLCanvasElement;
const container = document.getElementById('container') as HTMLDivElement;
// The readouts and the controls are optional: a page that only wants to show the result leaves the
// elements out, and everything below then just does nothing.
const hud = document.getElementById('hud') as HTMLDivElement | null;
const statsElement = document.getElementById('stats') as HTMLPreElement | null;

// The page bundle is emitted once per worker backend, and the HTML page declares which one it is.
const useWebGPU = new URL(import.meta.url).searchParams.has('webgpu');

// Resolved against this bundle's own URL so that each build directory loads its own worker script.
const worker = new Worker(new URL('./worker-render.js', import.meta.url), {type: 'module'});

// Keeps only the most recent lines: during a drag a resize line arrives per step, and appending to a
// DOM node on every one of them is main thread work that the whole point of this demo is to avoid.
const HUD_MAX_LINES = 12;
const hudLines: string[] = [];
const log = (text: string) => {
    if (!hud) {
        return;
    }
    hudLines.push(text);
    if (hudLines.length > HUD_MAX_LINES) {
        hudLines.splice(0, hudLines.length - HUD_MAX_LINES);
    }
    hud.textContent = hudLines.join('\n') + '\n';
};

// The gesture helpers in common.ts talk to a TGFXBaseView. On the page side there is no such object
// any more, so this forwards the same calls to the worker as messages and lets the existing gesture
// handling stay untouched.
class WorkerViewProxy {
    updateZoomScaleAndOffset(zoom: number, offsetX: number, offsetY: number) {
        worker.postMessage({type: 'zoom', zoom, offsetX, offsetY});
    }

    updateLayerTree(drawIndex: number) {
        worker.postMessage({type: 'layerTree', drawIndex});
    }
}

const shareData = new ShareData();
shareData.tgfxBaseView = new WorkerViewProxy() as any;

worker.onmessage = (event: MessageEvent) => {
    const message = event.data;
    switch (message.type) {
        case 'ready':
            log('worker ready: ' + message.width + 'x' + message.height + ' @' + message.density +
                ' (sizing=' + sizingMode + ')');
            syncBoxToBacking(message.width, message.height);
            break;
        case 'resized':
            // The count makes it obvious how many times the worker had to reallocate the drawing
            // buffer and redraw while a drag was in progress.
            resizeApplies++;
            log('worker resized: ' + message.width + 'x' + message.height + '  (#' + resizeApplies + ')');
            syncBoxToBacking(message.width, message.height);
            break;
        case 'stats':
            if (statsElement) {
                statsElement.textContent =
                    'worker fps           ' + message.fps.toFixed(1) + '\n' +
                    'frame gap max (win)  ' + message.maxFrameInterval.toFixed(1) + ' ms\n' +
                    'frame gap max (ever) ' + message.maxFrameIntervalEver.toFixed(1) + ' ms\n' +
                    'resize apply last    ' + message.lastApplyMs.toFixed(1) + ' ms\n' +
                    'resize apply max     ' + message.maxApplyMs.toFixed(1) + ' ms\n' +
                    'resize applies       ' + message.applyCount;
            }
            break;
        case 'error':
            log('worker error: ' + message.message);
            console.error('worker error: ' + message.message);
            break;
        default:
            break;
    }
};
worker.onerror = (error) => {
    log('worker crashed: ' + (error.message || error));
    console.error(error);
};

let layoutScheduled = false;
const layout = {cssWidth: 1, cssHeight: 1, dpr: 1};
// 'container' (default): the canvas fills the container through CSS, so it always matches the
// layout, but a frame whose backing store has not caught up yet gets resampled by the browser.
// 'backing': the CSS box is derived from the backing store the worker reports, so the browser never
// resamples the canvas at all; the price is that the box can lag the layout by a frame.
// Switch with ?sizing=backing to compare.
// Both of these are trade-offs that only show up on a heavy layer tree, so they are switchable at
// runtime from the control bar instead of being baked in.
let sizingMode: 'container' | 'backing' =
    new URLSearchParams(location.search).get('sizing') === 'backing' ? 'backing' : 'container';
// Applying every resize is the default. Resize work runs inside the frame callback now, so it can no
// longer pile up and starve the render loop, which is what used to make it feel like the canvas froze
// and then refreshed. A headless software rasteriser still shows a large frame gap here because the
// tile re-raster is done on the CPU; on a real GPU the same work is far cheaper. Raise this if a heavy
// layer tree on a slow machine starts to stutter.
let resizeIntervalMs = Number(new URLSearchParams(location.search).get('resize') ?? 0);
// The last backing store size the worker reported. The placeholder canvas only mirrors it late, so
// the reported value is the one to trust.
let lastBacking = {width: 0, height: 0};
let resizeApplies = 0;
// Until init has been posted the worker has no canvas, so a resize has nothing to be applied to.
// init carries the layout that is current at that moment, which keeps this race harmless.
let workerInitialized = false;

function scheduleLayout() {
    if (layoutScheduled) {
        return;
    }
    layoutScheduled = true;
    // Coalesced onto a frame boundary. Doing the layout off the frame boundary costs far more than
    // the resize itself, so it is deliberately not debounced with a timer.
    requestAnimationFrame(applyLayout);
}

// Only reports the container size now: the canvas fills the container through CSS, so the page
// never has to write its style, and the worker owns the backing store size.
function readLayout() {
    const rect = container.getBoundingClientRect();
    layout.cssWidth = Math.max(1, Math.round(rect.width));
    layout.cssHeight = Math.max(1, Math.round(rect.height));
    layout.dpr = window.devicePixelRatio || 1;
}

const sizingSelect = document.getElementById('sizing') as HTMLSelectElement | null;
const resizeSelect = document.getElementById('resize') as HTMLSelectElement | null;
const resetStatsButton = document.getElementById('resetStats') as HTMLButtonElement | null;
if (sizingSelect) {
    sizingSelect.value = sizingMode;
    sizingSelect.addEventListener('change', () => {
        sizingMode = sizingSelect.value as 'container' | 'backing';
        applySizingMode();
        log('sizing -> ' + sizingMode);
    });
}
if (resetStatsButton) {
    resetStatsButton.addEventListener('click', () => {
        // Zeroes the "ever" maxima so a single drag can be measured on its own.
        worker.postMessage({type: 'config', resetStats: true});
        log('stats reset');
    });
}
if (resizeSelect) {
    resizeSelect.value = String(resizeIntervalMs);
    resizeSelect.addEventListener('change', () => {
        resizeIntervalMs = Number(resizeSelect.value);
        // The worker owns the resize handling, so it needs to be told.
        worker.postMessage({type: 'config', resizeIntervalMs});
        log('resize apply interval -> ' + resizeIntervalMs + ' ms');
    });
}

function applyLayout() {
    layoutScheduled = false;
    readLayout();
    if (workerInitialized) {
        worker.postMessage({type: 'resize', ...layout});
    }
}

// In 'backing' mode the CSS box is pinned to the size the worker actually rendered at, so the
// browser never has to resample the canvas. In 'container' mode the canvas fills the container
// through CSS instead, which keeps it flush with the layout but lets the browser scale a frame whose
// backing store has not caught up.
function applySizingMode() {
    const dpr = layout.dpr || 1;
    if (sizingMode !== 'backing' || !lastBacking.width) {
        document.body.classList.remove('sizing-backing');
        canvas.style.width = '';
        canvas.style.height = '';
        return;
    }
    document.body.classList.add('sizing-backing');
    canvas.style.width = lastBacking.width / dpr + 'px';
    canvas.style.height = lastBacking.height / dpr + 'px';
}

function syncBoxToBacking(backingWidth: number, backingHeight: number) {
    lastBacking = {width: backingWidth, height: backingHeight};
    applySizingMode();
}

window.onload = async () => {
    try {
        // Fetch everything up front so the hand-over can happen in a single transfer.
        const [bridge, tgfx, fontBuffer, emojiBuffer] = await Promise.all([
            loadImage('resources/assets/bridge.jpg'),
            loadImage('resources/assets/tgfx.png'),
            fetch('resources/font/NotoSansSC-Regular.otf').then((response) => response.arrayBuffer()),
            fetch('resources/font/NotoColorEmoji.ttf').then((response) => response.arrayBuffer()),
        ]);
        const [bridgeBitmap, tgfxBitmap] = await Promise.all([
            createImageBitmap(bridge),
            createImageBitmap(tgfx),
        ]);

        // Take the layout before the hand-over so the worker starts at the size the window has now,
        // even if it changed while the assets were loading.
        readLayout();
        const offscreen = canvas.transferControlToOffscreen();
        log('canvas transferred to OffscreenCanvas: ' + offscreen.width + 'x' + offscreen.height);

        worker.postMessage({
            type: 'init',
            canvas: offscreen,
            useWebGPU,
            ...layout,
            images: [{name: 'bridge', bitmap: bridgeBitmap}, {name: 'TGFX', bitmap: tgfxBitmap}],
            fonts: {default: fontBuffer, emoji: emojiBuffer},
            resizeIntervalMs,
        }, [offscreen, bridgeBitmap, tgfxBitmap, fontBuffer, emojiBuffer]);
        workerInitialized = true;

        bindCanvasZoomAndPanEvents(canvas, shareData);
    } catch (error) {
        log('init failed: ' + (error as Error).message);
        console.error(error);
    }
};

// common.ts's onResizeEvent()/updateSize() both write canvas.width, which throws InvalidStateError on
// a transferred canvas, so neither is used here. scheduleLayout() replaces them.
window.onresize = () => {
    scheduleLayout();
};

window.onclick = () => {
    onClickEvent(shareData);
};
