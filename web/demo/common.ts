/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
    public updateSize: (devicePixelRatio: number) => void;
    public draw: (drawIndex: number, zoom: number, offsetX: number, offsetY: number) => boolean;
}

export class ShareData {
    public Hello2DModule: types.TGFX = null;
    public tgfxBaseView: TGFXBaseView = null;
    public drawIndex: number = 0;
    public resized: boolean = false;
    public zoom: number = 1.0;
    public offsetX: number = 0;
    public offsetY: number = 0;
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

enum DeviceKind {
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

    private handleScrollEvent(
        event: WheelEvent,
        state: ScrollGestureState,
        shareData: ShareData,
        deviceKind: DeviceKind = DeviceKind.MOUSE,
    ) {
        if (state === ScrollGestureState.SCROLL_CHANGE){
            this.scaleStartZoom = shareData.zoom;
            this.scaleY = 1.0;
            if (event.shiftKey && event.deltaX === 0 && event.deltaY !== 0) {
                shareData.offsetX -= event.deltaY * window.devicePixelRatio;
            } else {
                shareData.offsetX -= event.deltaX * window.devicePixelRatio;
                shareData.offsetY -= event.deltaY * window.devicePixelRatio;
            }
            draw(shareData);
        }
    }

    private handleScaleEvent(
        event: WheelEvent,
        state: ScaleGestureState,
        canvas: HTMLElement,
        shareData: ShareData,
        deviceKind: DeviceKind = DeviceKind.MOUSE
    ) {
        if (state === ScaleGestureState.SCALE_START) {
            this.scaleY = 1.0;
            this.scaleStartZoom = shareData.zoom;
        }
        if (state === ScaleGestureState.SCALE_CHANGE) {
            const rect = canvas.getBoundingClientRect();
            const pixelX = (event.clientX - rect.left) * window.devicePixelRatio;
            const pixelY = (event.clientY - rect.top) * window.devicePixelRatio;
            const newZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, this.scaleStartZoom * this.scaleY));
            shareData.offsetX = (shareData.offsetX - pixelX) * (newZoom / shareData.zoom) + pixelX;
            shareData.offsetY = (shareData.offsetY - pixelY) * (newZoom / shareData.zoom) + pixelY;
            shareData.zoom = newZoom;
            draw(shareData);
        }
        if (state === ScaleGestureState.SCALE_END){
            this.scaleY = 1.0;
            this.scaleStartZoom = shareData.zoom;
        }
    }

    public clearState() {
        this.scaleY = 1.0;
        this.timer = undefined;
    }

    private reserveScrollTimeout(
        event: WheelEvent,
        shareData: ShareData,
        deviceKind: DeviceKind = DeviceKind.MOUSE
    ) {
        clearTimeout(this.timer);
        this.timer = window.setTimeout(() => {
            this.timer = undefined;
            this.handleScrollEvent(event, ScrollGestureState.SCROLL_END, shareData, deviceKind);
            this.clearState();
        }, this.pinchTimeout);
    }

    private reserveScaleTimeout(
        event: WheelEvent,
        canvas: HTMLElement,
        shareData: ShareData,
        deviceKind: DeviceKind = DeviceKind.MOUSE
    ) {
        clearTimeout(this.timer);
        this.timer = window.setTimeout(() => {
            this.timer = undefined;
            this.handleScaleEvent(event, ScaleGestureState.SCALE_END, canvas, shareData, deviceKind);
            this.clearState();
        }, this.pinchTimeout);
    }

    private getDeviceKind(event: WheelEvent): DeviceKind {
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
        return isTouchpad ? DeviceKind.TOUCH : DeviceKind.MOUSE;
    }

    public onWheel(event: WheelEvent, canvas: HTMLElement, shareData: ShareData) {
        const deviceKind = this.getDeviceKind(event);
        let wheelRatio = this.touchWheelRatio;
        if (deviceKind === DeviceKind.MOUSE) {
            wheelRatio = this.mouseWheelRatio;
        }
        if (!event.deltaY || (!event.ctrlKey && !event.metaKey)) {
            this.reserveScrollTimeout(event, shareData, deviceKind);
            this.handleScrollEvent(event, ScrollGestureState.SCROLL_CHANGE, shareData, deviceKind);
            return;
        }
        this.scaleY *= Math.exp(-(event.deltaY) / wheelRatio);
        if (!this.timer) {
            this.reserveScaleTimeout(event, canvas, shareData, deviceKind);
            this.handleScaleEvent(event, ScaleGestureState.SCALE_START, canvas, shareData, deviceKind);
        } else {
            this.reserveScaleTimeout(event, canvas, shareData, deviceKind);
            this.handleScaleEvent(event, ScaleGestureState.SCALE_CHANGE, canvas, shareData, deviceKind);
        }
    }
}

let canDraw = true;
const gestureManager: GestureManager = new GestureManager();

function isPromise(obj: any): obj is Promise<any> {
    return !!obj && typeof obj.then === "function";
}

function draw(shareData: ShareData) {
    if (canDraw === true) {
        canDraw = false;
        const result = shareData.tgfxBaseView.draw(
            shareData.drawIndex,
            shareData.zoom,
            shareData.offsetX,
            shareData.offsetY
        );
        if (isPromise(result)) {
            result.then((res: boolean) => {
                canDraw = res;
            });
        } else {
            canDraw = result;
        }
    }
}

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
    shareData.tgfxBaseView.updateSize(scaleFactor);
    draw(shareData);
}

export function onResizeEvent(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.resized = true;
}

export function onClickEvent(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.drawIndex++;
    shareData.offsetX = 0;
    shareData.offsetY = 0;
    shareData.zoom = 1.0;
    gestureManager.clearState();
    draw(shareData);
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
    if (!canvas) return;

    canvas.addEventListener('wheel', (e: WheelEvent) => {
        e.preventDefault();
        gestureManager.onWheel(e, canvas, shareData);
    }, { passive: false });
}
