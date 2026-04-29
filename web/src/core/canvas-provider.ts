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
 *
 * Implementations MUST always return a valid canvas from getCanvas2D; they
 * should throw or fall back internally rather than returning null, so that
 * callers of getCanvasProvider() do not need to scatter null checks.
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
    // Shallow-copy so that later mutations on the caller's object cannot
    // silently change the active provider behind setCanvasProvider's back.
    canvasProvider = { ...provider };
}

/**
 * Returns the currently active canvas provider. The returned reference is
 * marked Readonly to discourage callers from mutating the internal state;
 * use setCanvasProvider to change the active provider.
 */
export function getCanvasProvider(): Readonly<CanvasProvider> {
    return canvasProvider;
}
