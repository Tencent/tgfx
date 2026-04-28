/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

import {getCanvas2D as defaultGetCanvas2D, releaseCanvas2D as defaultReleaseCanvas2D} from '../utils/canvas';

/**
 * Abstraction over the underlying 2D canvas pool so that platform-specific
 * implementations (e.g. the WeChat mini program) can supply their own canvas
 * creation and recycling routines without duplicating rasterization logic.
 */
export interface CanvasProvider {
    getCanvas2D: (width: number, height: number) => HTMLCanvasElement | OffscreenCanvas;
    releaseCanvas2D: (canvas: HTMLCanvasElement | OffscreenCanvas) => void;
}

let canvasProvider: CanvasProvider = {
    getCanvas2D: defaultGetCanvas2D,
    releaseCanvas2D: defaultReleaseCanvas2D,
};

/**
 * Replaces the active canvas provider. Platform entry points (such as
 * TGFXBind for WeChat) must call this before any rasterizer constructs
 * its first canvas, otherwise the browser-based default will be used.
 */
export function setCanvasProvider(provider: CanvasProvider) {
    canvasProvider = provider;
}

/**
 * Returns the currently active canvas provider.
 */
export function getCanvasProvider(): CanvasProvider {
    return canvasProvider;
}
