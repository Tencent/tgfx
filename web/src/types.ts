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

export declare class Vector<T> {
    private constructor();

    /**
     * Get item from Vector by index.
     */
    public get(index: number): T;

    /**
     * Push item into Vector.
     */
    public push_back(value: T): void;

    /**
     * Get item number in Vector.
     */
    public size(): number;

    /**
     * Delete Vector instance.
     */
    public delete(): void;
}


export const enum MatrixIndex {
    a,
    c,
    tx,
    b,
    d,
    ty,
}

export interface ctor {
    value: number;
}

export interface Point {
    x: number;
    y: number;
}

export interface Rect {
    /**
     * smaller x-axis bounds.
     */
    left: number;
    /**
     * smaller y-axis bounds.
     */
    top: number;
    /**
     * larger x-axis bounds.
     */
    right: number;
    /**
     * larger y-axis bounds.
     */
    bottom: number;
}

export interface TGFXLineCap {
    /**
     * No stroke extension.
     */
    Butt: ctor;
    /**
     * Adds circle
     */
    Round: ctor;
    /**
     * Adds square
     */
    Square: ctor;
}

export interface TGFXLineJoin {
    /**
     * Extends to miter limit.
     */
    Miter: ctor;
    /**
     * Adds circle.
     */
    Round: ctor;
    /**
     * Connects outside edges.
     */
    Bevel: ctor;
}

export interface TGFXPathFillType {
    /**
     * Enclosed by a non-zero sum of contour directions.
     */
    Winding: ctor;
    /**
     * Enclosed by an odd number of contours.
     */
    EvenOdd: ctor;
    /**
     * Enclosed by a zero sum of contour directions.
     */
    InverseWinding: ctor;
    /**
     * Enclosed by an even number of contours.
     */
    InverseEvenOdd: ctor;
}


export interface EmscriptenGLContext {
    handle: number;
    GLctx: WebGLRenderingContext;
    attributes: EmscriptenGLContextAttributes;
    initExtensionsDone: boolean;
    version: number;
}

export type EmscriptenGLContextAttributes =
    { majorVersion: number; minorVersion: number }
    & WebGLContextAttributes;

export interface EmscriptenGL {
    contexts: (EmscriptenGLContext | null)[];
    createContext: (
        canvas: HTMLCanvasElement | OffscreenCanvas,
        webGLContextAttributes: EmscriptenGLContextAttributes,
    ) => number;
    currentContext?: EmscriptenGLContext;
    deleteContext: (contextHandle: number) => void;
    framebuffers: (WebGLFramebuffer | null)[];
    getContext: (contextHandle: number) => EmscriptenGLContext;
    getNewId: (array: any[]) => number;
    makeContextCurrent: (contextHandle: number) => boolean;
    registerContext: (ctx: WebGLRenderingContext, webGLContextAttributes: EmscriptenGLContextAttributes) => number;
    textures: (WebGLTexture | null)[];
}

export interface TGFX extends EmscriptenModule {
    TGFXLineCap: TGFXLineCap;
    TGFXLineJoin: TGFXLineJoin;
    GL: EmscriptenGL;
    TGFXPathFillType: TGFXPathFillType;
    _Matrix: {
        _MakeAll: (
            scaleX: number,
            skewX: number,
            transX: number,
            skewY: number,
            scaleY: number,
            transY: number,
            pers0: number,
            pers1: number,
            pers2: number,
        ) => any;
        _MakeScale: ((sx: number, sy: number) => any) & ((scale: number) => any);
        _MakeTrans: (dx: number, dy: number) => any;
    };

    [key: string]: any;
}