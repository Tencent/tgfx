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

export const ItemCount: number = 20000;
const Width: number = 720 * 2;
const Height: number = 1024 * 2;
let startStatus = false;
let startTime = -1;
let renderFrames = 0;
let textElement = null;

export class TGFXBaseView {
    public updateSize: (devicePixelRatio: number) => void;
    public draw: (drawIndex: number) => void;
}

export class ShareData {
    public Hello2DModule: types.TGFX = null;
    public tgfxBaseView: TGFXBaseView = null;
    public drawIndex: number = 0;
    public resized: boolean = false;
}

export function updateSize(shareData: ShareData) {
    if (!shareData.tgfxBaseView) {
        return;
    }
    shareData.resized = false;
    let canvas = document.getElementById('hello2d') as HTMLCanvasElement;
    canvas.style.top = '80px';
    canvas.style.position = 'absolute';
    canvas.style.left = '0';
    canvas.style.right = '0';
    canvas.style.marginLeft = 'auto';
    canvas.style.marginRight = 'auto';
    let container = document.getElementById('container') as HTMLDivElement;
    // let screenRect = container.getBoundingClientRect();
    let scaleFactor = 1;
    canvas.width = Width;
    canvas.height = Height;
    canvas.style.width = (Width / 2) + "px";
    canvas.style.height = (Height /2)+ "px";
    shareData.tgfxBaseView.updateSize(scaleFactor);
    shareData.tgfxBaseView.draw(shareData.drawIndex);
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
    shareData.tgfxBaseView.draw(shareData.drawIndex);
}

export function loadImage(src: string) {
    return new Promise((resolve, reject) => {
        let img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = src;
    })
}

export function setStartStatus(status: boolean) {
    startStatus = status;
}

export function addTextElement() {
    textElement = document.createElement('div')
    textElement.textContent = "TGFX Test";
    textElement.style.position = 'absolute';
    textElement.style.left = '50%';
    textElement.style.top = '20px';
    textElement.style.transform = 'translateX(-50%)';
    textElement.style.fontSize = '40px';
    textElement.style.color = '#000000';
    document.body.appendChild(textElement);
}
export function ShowFPS(shareData: ShareData) {
    if (startStatus) {
        if (startTime < 0) {
            startTime = new Date().getTime();
        }
        onclickEvent(shareData);
        renderFrames++;
        const currentTimestamp = new Date().getTime();
        const timeOffset = currentTimestamp - startTime;
        if (timeOffset >= 1000) {
            const fps = renderFrames * 1000.0 / timeOffset;
            textElement.textContent = `Count: ${ItemCount}, FPS: ${fps.toFixed(2)}`;
            renderFrames = 0;
            startTime = currentTimestamp;
        }
    }
    requestAnimationFrame(() => ShowFPS(shareData));
}