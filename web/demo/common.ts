/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

import * as types from '../types/types';

export const MIN_ZOOM = 0.001;
export const MAX_ZOOM = 1000.0;

export class TGFXBaseView {
    public updateSize: () => void;
    public updateDisplayList: (drawIndex: number) => void;
    public updateDisplayTransform: (zoom: number, offsetX: number, offsetY: number) => void;
    public draw: () => void;
}

export class ShareData {
    public Hello2DModule: types.TGFX = null;
    public tgfxBaseView: TGFXBaseView = null;
    public drawIndex: number = 0;
    public animationFrameId: number | null = null;
    public isPageVisible: boolean = true;
    public resized: boolean = false;
}

enum ScaleGestureState {
    SCALE_START = 0,
    SCALE_CHANGE = 1,
    SCALE_END = 2,
}

enum ScrollGestureState {
    SCROLL_START = 0,
    SCROLL_CHANGE = 1,
    SCROLL_END = 2,
}

enum DeviceType {
    TOUCH = 0,
    MOUSE = 1,
}

class GestureManager {
    private scaleY = 1.0;
    private pinchTimeout = 150;
    private timer: number | undefined;
    private scaleStartZoom = 1.0;

    private lastEventTime = 0;
    private lastDeltaY = 0;
    private timeThreshold = 50; 
    private deltaYThreshold = 50;
    private deltaYChangeThreshold = 10;
    private mouseWheelRatio = 800;
    private touchWheelRatio = 100;

    public zoom = 1.0;
    public offsetX = 0;
    public offsetY = 0;

    private handleScrollEvent(
        event: WheelEvent,
        state: ScrollGestureState,
        shareData: ShareData,
        deviceType: DeviceType = DeviceType.MOUSE,
    ) {
        if (state === ScrollGestureState.SCROLL_CHANGE){
            this.scaleStartZoom = this.zoom;
            this.scaleY = 1.0;
            if (event.shiftKey && event.deltaX === 0 && event.deltaY !== 0) {
                this.offsetX -= event.deltaY * window.devicePixelRatio;
            } else {
                this.offsetX -= event.deltaX * window.devicePixelRatio;
                this.offsetY -= event.deltaY * window.devicePixelRatio;
            }
            shareData.tgfxBaseView.updateDisplayTransform(this.zoom, this.offsetX, this.offsetY);
        }
    }

    private handleScaleEvent(
        event: WheelEvent,
        state: ScaleGestureState,
        canvas: HTMLElement,
        shareData: ShareData,
        deviceType: DeviceType = DeviceType.MOUSE
    ) {
        if (state === ScaleGestureState.SCALE_START) {
            this.scaleY = 1.0;
            this.scaleStartZoom = this.zoom;
        }
        if (state === ScaleGestureState.SCALE_CHANGE) {
            const rect = canvas.getBoundingClientRect();
            const pixelX = (event.clientX - rect.left) * window.devicePixelRatio;
            const pixelY = (event.clientY - rect.top) * window.devicePixelRatio;
            const newZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, this.scaleStartZoom * this.scaleY));
            this.offsetX = (this.offsetX - pixelX) * (newZoom / this.zoom) + pixelX;
            this.offsetY = (this.offsetY - pixelY) * (newZoom / this.zoom) + pixelY;
            this.zoom = newZoom;
        }
        if (state === ScaleGestureState.SCALE_END){
            this.scaleY = 1.0;
            this.scaleStartZoom = this.zoom;
        }
        shareData.tgfxBaseView.updateDisplayTransform(this.zoom, this.offsetX, this.offsetY);
    }

    public clearState() {
        this.scaleY = 1.0;
        this.timer = undefined;
    }

    public resetTransform() {
        this.zoom = 1.0;
        this.offsetX = 0;
        this.offsetY = 0;
        this.clearState();
    }

    private resetScrollTimeout(
        event: WheelEvent,
        shareData: ShareData,
        deviceType: DeviceType = DeviceType.MOUSE
    ) {
        clearTimeout(this.timer);
        this.timer = window.setTimeout(() => {
            this.timer = undefined;
            this.handleScrollEvent(event, ScrollGestureState.SCROLL_END, shareData, deviceType);
            this.clearState();
        }, this.pinchTimeout);
    }

    private resetScaleTimeout(
        event: WheelEvent,
        canvas: HTMLElement,
        shareData: ShareData,
        deviceType: DeviceType = DeviceType.MOUSE
    ) {
        clearTimeout(this.timer);
        this.timer = window.setTimeout(() => {
            this.timer = undefined;
            this.handleScaleEvent(event, ScaleGestureState.SCALE_END, canvas, shareData, deviceType);
            this.clearState();
        }, this.pinchTimeout);
    }

    private getDeviceType(event: WheelEvent): DeviceType {
        const now = Date.now();
        const timeDifference = now - this.lastEventTime;
        const deltaYChange = Math.abs(event.deltaY - this.lastDeltaY);
        let isTouchpad = false;
        if (event.deltaMode === event.DOM_DELTA_PIXEL && timeDifference < this.timeThreshold) {
            if (Math.abs(event.deltaY) < this.deltaYThreshold && deltaYChange < this.deltaYChangeThreshold) {
                isTouchpad = true;
            }
        }
        this.lastEventTime = now;
        this.lastDeltaY = event.deltaY;
        return isTouchpad ? DeviceType.TOUCH : DeviceType.MOUSE;
    }

    public onWheel(event: WheelEvent, canvas: HTMLElement, shareData: ShareData) {
        const deviceType = this.getDeviceType(event);
        let wheelRatio = (deviceType === DeviceType.MOUSE ? this.mouseWheelRatio : this.touchWheelRatio);
        if (!event.deltaY || (!event.ctrlKey && !event.metaKey)) {
            this.resetScrollTimeout(event, shareData, deviceType);
            this.handleScrollEvent(event, ScrollGestureState.SCROLL_CHANGE, shareData, deviceType);
        } else {
            this.scaleY *= Math.exp(-(event.deltaY) / wheelRatio);
            if (!this.timer) {
                this.resetScaleTimeout(event, canvas, shareData, deviceType);
                this.handleScaleEvent(event, ScaleGestureState.SCALE_START, canvas, shareData, deviceType);
            } else {
                this.resetScaleTimeout(event, canvas, shareData, deviceType);
                this.handleScaleEvent(event, ScaleGestureState.SCALE_CHANGE, canvas, shareData, deviceType);
            }
        }
    }
}

const gestureManager: GestureManager = new GestureManager();

function draw(shareData: ShareData): void {
    shareData.tgfxBaseView.draw();
}
let animationLoopRunning = false;

export function updateSize(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.resized = false;
    const canvas = document.getElementById('hello2d') as HTMLCanvasElement;
    const container = document.getElementById('container') as HTMLDivElement;
    const screenRect = container.getBoundingClientRect();
    const scaleFactor = window.devicePixelRatio;
    canvas.width = screenRect.width * scaleFactor;
    canvas.height = screenRect.height * scaleFactor;
    canvas.style.width = screenRect.width + "px";
    canvas.style.height = screenRect.height + "px";
    shareData.tgfxBaseView.updateSize();
}

export function onResizeEvent(shareData: ShareData) {
    if (!shareData.tgfxBaseView || shareData.resized) {
        return;
    }
    shareData.resized = true;
    window.setTimeout(() => {
        updateSize(shareData);
    }, 300);
}

function handleVisibilityChange(shareData: ShareData) {
    shareData.isPageVisible = !document.hidden;
    if (shareData.isPageVisible && shareData.animationFrameId === null) {
        animationLoop(shareData);
    }
}

export function animationLoop(shareData: ShareData) {
    if (animationLoopRunning) {
        return;
    }
    animationLoopRunning = true;

    const frame = () => {
        if (!shareData.tgfxBaseView || !shareData.isPageVisible) {
            animationLoopRunning = false;
            shareData.animationFrameId = null;
            return;
        }

        draw(shareData);
        shareData.animationFrameId = requestAnimationFrame(frame);
    };
    shareData.animationFrameId = requestAnimationFrame(frame);
}

function setupDevicePixelRatioListener(shareData: ShareData) {
    const updateOnDPRChange = () => {
        updateSize(shareData);
        setupDevicePixelRatioListener(shareData);
    };
    const mediaQuery = window.matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`);
    mediaQuery.addEventListener('change', updateOnDPRChange, { once: true });
}

function setupCanvasResizeObserver(shareData: ShareData) {
    const canvas = document.getElementById('hello2d') as HTMLCanvasElement;
    if (!canvas) {
        return;
    }
    let lastDevicePixelRatio = window.devicePixelRatio;
    const resizeObserver = new ResizeObserver(() => {
        if (window.devicePixelRatio !== lastDevicePixelRatio) {
            lastDevicePixelRatio = window.devicePixelRatio;
            updateSize(shareData);
        }
    });
    resizeObserver.observe(canvas);
}

export function setupVisibilityListeners(shareData: ShareData) {
    if (typeof window !== 'undefined') {
        document.addEventListener('visibilitychange', () => handleVisibilityChange(shareData));
        window.addEventListener('beforeunload', () => {
            if (shareData.animationFrameId !== null) {
                cancelAnimationFrame(shareData.animationFrameId);
                shareData.animationFrameId = null;
            }
        });
        setupDevicePixelRatioListener(shareData);
        setupCanvasResizeObserver(shareData);
    }
}

export function onClickEvent(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.drawIndex++;
    gestureManager.resetTransform();
    shareData.tgfxBaseView.updateDisplayTransform(gestureManager.zoom, gestureManager.offsetX, gestureManager.offsetY);
    shareData.tgfxBaseView.updateDisplayList(shareData.drawIndex);
}

export function loadImage(src: string): Promise<HTMLImageElement> {
    return new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = src;
    });
}

export function bindCanvasZoomAndPanEvents(canvas: HTMLElement, shareData: ShareData) {
    if (!canvas) {
        return;
    }

    canvas.addEventListener('wheel', (e: WheelEvent) => {
        e.preventDefault();
        gestureManager.onWheel(e, canvas, shareData);
    }, { passive: false });
}
