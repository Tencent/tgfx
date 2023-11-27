/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
import {TGFXBind} from '../lib/tgfx';
import Hello2D from './wasm/hello2d';

declare class TGFXView {
    public static MakeFrom: (selector: string) => TGFXView;
    public updateSize: (devicePixelRatio: number) => void;
    public draw: (drawIndex: number) => void;
}

let Hello2DModule: types.TGFX = null;
let tgfxView: TGFXView = null;
let drawIndex: number = 0;
let resized: boolean = false;

function updateSize() {
    if (!tgfxView) {
        return;
    }
    resized = false;
    let canvas = document.getElementById('hello2d') as HTMLCanvasElement;
    let container = document.getElementById('container') as HTMLDivElement;
    let screenRect = container.getBoundingClientRect();
    let scaleFactor = window.devicePixelRatio;
    canvas.width = screenRect.width * scaleFactor;
    canvas.height = screenRect.height * scaleFactor;
    canvas.style.width = screenRect.width + "px";
    canvas.style.height = screenRect.height + "px";
    tgfxView.updateSize(scaleFactor);
    tgfxView.draw(drawIndex);
}

const loadImage = (src) => {
    return new Promise((resolve, reject) => {
        let img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = src;
    })
}

window.onload = async () => {
    Hello2DModule = await Hello2D({locateFile: (file: string) => './wasm/' + file})
        .then((module: types.TGFX) => {
            TGFXBind(module);
            return module;
        })
        .catch((error: any) => {
            console.error(error);
            throw new Error("Hello2D init failed. Please check the .wasm file path!.");
        });
    let image = await loadImage('../../resources/assets/bridge.jpg');
    tgfxView = Hello2DModule.TGFXView.MakeFrom('#hello2d', image);
    updateSize();
};

window.onresize = () => {
    if (resized) {
        return;
    }
    resized = true;
    window.setTimeout(updateSize, 300);
};

window.onclick = () => {
    if (!tgfxView) {
        return;
    }
    drawIndex++;
    tgfxView.draw(drawIndex);
};
