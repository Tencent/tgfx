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

export const MinZoom = 0.001;
export const MaxZoom = 1000.0;
export const ZoomFactorPerNotch = 1.1;

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

let canDraw = true;

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
    let canvas = document.getElementById('hello2d') as HTMLCanvasElement;
    let container = document.getElementById('container') as HTMLDivElement;
    let screenRect = container.getBoundingClientRect();
    let scaleFactor = window.devicePixelRatio;
    canvas.width = screenRect.width * scaleFactor;
    canvas.height = screenRect.height * scaleFactor;
    canvas.style.width = screenRect.width + "px";
    canvas.style.height = screenRect.height + "px";
    shareData.tgfxBaseView.updateSize(scaleFactor);
    draw(shareData);
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
    draw(shareData);
}

export function loadImage(src: string) {
    return new Promise((resolve, reject) => {
        let img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = src;
    })
}

export function bindCanvasZoomAndPanEvents(canvas: HTMLElement, shareData: ShareData) {
    if (!canvas) return;
    window.addEventListener('mouseup', () => {
        shareData.offsetX = 0;
        shareData.offsetY = 0;
        shareData.zoom = 1.0;
    });

    canvas.addEventListener('wheel', (e: WheelEvent) => {
        e.preventDefault();
        if (e.ctrlKey || e.metaKey) {
            const newZoom = Math.max(MinZoom, Math.min(MaxZoom, shareData.zoom * normalizeZoom(e)));
            const rect = canvas.getBoundingClientRect();
            const px = (e.clientX - rect.left) * window.devicePixelRatio;
            const py = (e.clientY - rect.top) * window.devicePixelRatio;
            shareData.offsetX = (shareData.offsetX - px) * (newZoom / shareData.zoom) + px;
            shareData.offsetY = (shareData.offsetY - py) * (newZoom / shareData.zoom) + py;
            shareData.zoom = newZoom;
        } else {
            if(e.shiftKey && e.deltaX === 0 && e.deltaY !== 0){
                shareData.offsetX -= e.deltaY * window.devicePixelRatio;
            }else{
                shareData.offsetX -= e.deltaX * window.devicePixelRatio;
                shareData.offsetY -= e.deltaY * window.devicePixelRatio;
            }
        }
        draw(shareData);
    }, { passive: false });
}

/**
 * Standardized mouse wheel deltaY to be consistent with VSCode's implementation, compatible with Chrome/Firefox/old API, different platforms and DPIs.
 * Refs VSCode: https://github.com/microsoft/vscode/blob/main/src/vs/base/browser/mouseEvent.ts
 */
export function normalizeZoom(e: WheelEvent): number {
    const ua = navigator.userAgent.toLowerCase();
    const isFirefox = ua.indexOf('firefox') > -1;
    const isSafari = /safari/.test(ua) && !/chrome/.test(ua);
    const isMac = navigator.platform.toUpperCase().indexOf('MAC') >= 0;
    const isWindows = navigator.platform.toUpperCase().indexOf('WIN') >= 0;
    let shouldFactorDPR = false;
    const chromeMatch = navigator.userAgent.match(/Chrome\/(\d+)/);
    if (chromeMatch) {
        const chromeVer = parseInt(chromeMatch[1]);
        shouldFactorDPR = chromeVer <= 122;
    }
    const devicePixelRatio = (e.view && (e.view as Window).devicePixelRatio) || 1;
    if (typeof (e as any).wheelDeltaY !== "undefined" && Math.abs((e as any).wheelDeltaY) >= 120) {
        let deltaY: number;
        if (shouldFactorDPR) {
            deltaY = (e as any).wheelDeltaY / (120 * devicePixelRatio);
        } else {
            // Differentiate between a normal scroll wheel and a touchpad
            if(Math.abs(e.deltaY) > 4){
                deltaY = (e as any).wheelDeltaY / 120;
            }else{
                deltaY = -e.deltaY / 120;
            }
        }
        if (isSafari && isWindows) {
            deltaY = -deltaY;
        }
        if(Math.abs(e.deltaY) < 4){
            return 1 + deltaY;
        }
        return Math.pow(ZoomFactorPerNotch, deltaY);
    }
    if (typeof (e as any).axis !== "undefined" && (e as any).axis === (e as any).VERTICAL_AXIS) {
        return 1-((e as any).detail) / 3;
    }
    if (e.type === "wheel") {
        if (e.deltaMode === WheelEvent.DOM_DELTA_LINE) {
            if (isFirefox && !isMac) {
                return 1-e.deltaY / 3;
            } else {
                return 1-e.deltaY;
            }
        }else if(e.deltaMode === WheelEvent.DOM_DELTA_PIXEL){
            return 1-e.deltaY / 120;
        }
        else {
            return 1-e.deltaY / 40;
        }
    }
    return 1-e.deltaY / 40;
}
