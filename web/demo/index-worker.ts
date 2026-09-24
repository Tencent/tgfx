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

// The page bundle is emitted once per worker backend, and the HTML page declares which one it is.
const useWebGPU = new URL(import.meta.url).searchParams.has('webgpu');

// Resolved against this bundle's own URL so that each build directory loads its own worker script.
const worker = new Worker(new URL('./worker-render.js', import.meta.url), {type: 'module'});

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

// Nothing on this page renders or reports, so the console is the only place a failure can show up.
worker.onmessage = (event: MessageEvent) => {
    if (event.data.type === 'error') {
        console.error('worker error: ' + event.data.message);
    }
};
worker.onerror = (error) => {
    console.error(error);
};

let layoutScheduled = false;
const layout = {cssWidth: 1, cssHeight: 1, dpr: 1};
// Applying every resize is the default. Resize work runs inside the frame callback, so it can no
// longer pile up and starve the render loop, which is what used to make the canvas freeze and then
// refresh. A heavy layer tree on a slow machine can be given one resize apply per interval instead
// with ?resize=<ms>.
let resizeIntervalMs = Number(new URLSearchParams(location.search).get('resize') ?? 0);
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

// Only reports the container size: the canvas fills the container through CSS, so the page never has
// to write its style, and the worker owns the backing store size.
function readLayout() {
    const rect = container.getBoundingClientRect();
    layout.cssWidth = Math.max(1, Math.round(rect.width));
    layout.cssHeight = Math.max(1, Math.round(rect.height));
    layout.dpr = window.devicePixelRatio || 1;
}

function applyLayout() {
    layoutScheduled = false;
    readLayout();
    if (workerInitialized) {
        worker.postMessage({type: 'resize', ...layout});
    }
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
