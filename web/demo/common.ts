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

export class TGFXBaseView {
    public updateSize: (devicePixelRatio: number) => void;
    public draw: (drawIndex: number, zoom: number, offsetX: number, offsetY: number) => void;
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

// Refs https://github.com/microsoft/vscode/blob/main/src/vs/base/browser/mouseEvent.ts
export function normalizeWheelDeltaY(e: WheelEvent): number {
    if (e.type === "wheel") {
        if (e.deltaMode === WheelEvent.DOM_DELTA_LINE) {
            const isFirefox = navigator.userAgent.toLowerCase().indexOf('firefox') > -1;
            const isMac = navigator.platform.toUpperCase().indexOf('MAC') >= 0;
            if (isFirefox && !isMac) {
                return -e.deltaY / 3;
            } else {
                return -e.deltaY;
            }
        } else {
            return -e.deltaY / 40;
        }
    }
    return -e.deltaY / 40;
}

export const MinZoom = 0.001;
export const MaxZoom = 1000.0;
export const ZoomFactorPerNotch = 1.1;

export function updateSize(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.resized = false;
    let canvas = document.getElementById('hello2d') as HTMLCanvasElement;
    let container = document.getElementById('container') as HTMLDivElement;
    let screenRect = container.getBoundingClientRect();
    let scaleFactor = window.devicePixelRatio;
    canvas.width = screenRect.width * scaleFactor;
    canvas.height = screenRect.height * scaleFactor;
    canvas.style.width = screenRect.width + "px";
    canvas.style.height = screenRect.height + "px";
    shareData.tgfxBaseView.updateSize(scaleFactor);
    shareData.tgfxBaseView.draw(shareData.drawIndex, shareData.zoom, shareData.offsetX, shareData.offsetY);
}

export function onresizeEvent(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.resized = true;
}

export function onclickEvent(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.drawIndex++;
    shareData.tgfxBaseView.draw(shareData.drawIndex, shareData.zoom, shareData.offsetX, shareData.offsetY);
}

export function loadImage(src: string) {
    return new Promise((resolve, reject) => {
        let img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = src;
    })
}

export function setupCommonCanvasEvents(canvas: HTMLElement, shareData: ShareData) {
    if (!canvas) return;
    window.addEventListener('mouseup', () => {
        shareData.offsetX = 0;
        shareData.offsetY = 0;
        shareData.zoom = 1.0;
    });

    canvas.addEventListener('wheel', (e: WheelEvent) => {
        e.preventDefault();
        if (e.ctrlKey || e.metaKey) {
            const newZoom = Math.max(MinZoom, Math.min(MaxZoom, shareData.zoom * Math.pow(ZoomFactorPerNotch, normalizeWheelDeltaY(e))));
            const rect = canvas.getBoundingClientRect();
            const px = (e.clientX - rect.left) * window.devicePixelRatio;
            const py = (e.clientY - rect.top) * window.devicePixelRatio;
            shareData.offsetX = (shareData.offsetX - px) * (newZoom / shareData.zoom) + px;
            shareData.offsetY = (shareData.offsetY - py) * (newZoom / shareData.zoom) + py;
            shareData.zoom = newZoom;
            shareData.tgfxBaseView.draw(shareData.drawIndex, shareData.zoom, shareData.offsetX, shareData.offsetY);
        } else {
            shareData.offsetX -= e.deltaX * window.devicePixelRatio;
            shareData.offsetY -= e.deltaY * window.devicePixelRatio;
            shareData.tgfxBaseView.draw(shareData.drawIndex, shareData.zoom, shareData.offsetX, shareData.offsetY);
        }
    }, { passive: false });
}
