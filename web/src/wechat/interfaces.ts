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

export interface wx {
    env: {
        USER_DATA_PATH: string;
    };
    getFileSystemManager: () => FileSystemManager;
    getFileInfo: (object: {
        filePath: string;
        success?: () => void;
        fail?: () => void;
        complete?: () => void
    }) => void;
    createVideoDecoder: () => VideoDecoder;
    getSystemInfoSync: () => SystemInfo;
    createOffscreenCanvas: (
        object?: { type: 'webgl' | '2d' },
        width?: number,
        height?: number,
        compInst?: any,
    ) => OffscreenCanvas;
    getPerformance: () => Performance;
}

export interface FileSystemManager {
    accessSync: (path: string) => void;
    mkdirSync: (path: string) => void;
    writeFileSync: (path: string, data: string | ArrayBuffer, encoding: string) => void;
    unlinkSync: (path: string) => void;
    readdirSync: (path: string) => string[];
}

export interface VideoDecoder {
    getFrameData: () => FrameDataOptions;
    seek: (
        /**
         * The position to seek to, in milliseconds
         */
        position: number,
    ) => Promise<void>;
    start: (option: VideoDecoderStartOption) => Promise<any>;
    remove: () => Promise<any>;
    off: (
        /**
         * Event name
         */
        eventName: string,
        /**
         * Callback function for the event.
         */
        callback: (...args: any[]) => any,
    ) => void;
    on: (
        /**
         * Event name
         *
         * The following events are supported:
         * - 'start': Start event. Returns { width, height };
         * - 'stop': Stop event.
         * - 'seek': Seek complete event.
         * - 'bufferchange': Buffer change event.
         * - 'ended': Decoding end event.
         */
        eventName: 'start' | 'stop' | 'seek' | 'bufferchange' | 'ended',
        /**
         * Callback function for the event.
         */
        callback: (...args: any[]) => any,
    ) => void;
}

/**
 * Video frame data, if not available, null is returned. When the buffer is empty, the data may not
 * be available.
 */
export interface FrameDataOptions {
    /**
     * Frame data
     */
    data: ArrayBuffer;
    /**
     * Frame data height
     */
    height: number;
    /**
     * Original dts of the frame
     */
    pkDts: number;
    /**
     * Original pts of the frame
     */
    pkPts: number;
    /**
     * Frame data width
     */
    width: number;
}

export interface VideoDecoderStartOption {
    /**
     * Video source file to be decoded. Versions below 2.13.0 of the base library only support local
     * paths. Starting from 2.13.0, remote paths of the http:// and https:// protocols are supported.
     */
    source: string;
    /**
     * Decoding mode. 0: Decode by pts; 1: Decode at the fastest speed
     */
    mode?: number;
}

export interface SystemInfo {
    /**
     * Client platform
     */
    platform: 'ios' | 'android' | 'windows' | 'mac' | 'devtools';
    /**
     * Pixel ratio of the device
     */
    pixelRatio: number;
}
