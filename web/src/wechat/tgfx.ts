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

import { getSourceSize, isAndroidMiniprogram } from '../tgfx';
import { ArrayBufferImage } from './array-buffer-image';
import { getCanvas2D, releaseCanvas2D } from './canvas';

import type { EmscriptenGL } from '../types';

type WxOffscreenCanvas = OffscreenCanvas & { isOffscreenCanvas: boolean };

export const uploadToTexture = (
  GL: EmscriptenGL,
  source: TexImageSource | ArrayBufferImage | WxOffscreenCanvas,
  textureID: number,
  alphaOnly: boolean,
) => {
  const gl = GL.currentContext?.GLctx as WebGLRenderingContext;
  gl.bindTexture(gl.TEXTURE_2D, GL.textures[textureID]);
  if (!alphaOnly && ('isOffscreenCanvas' in source) && source.isOffscreenCanvas) {
    const ctx = source.getContext('2d') as OffscreenCanvasRenderingContext2D;
    const imgData = ctx.getImageData(0, 0, source.width, source.height);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      imgData.width,
      imgData.height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      new Uint8Array(imgData.data),
    );
    return;
  }

  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
  if (source instanceof ArrayBufferImage) {
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      source.width,
      source.height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      new Uint8Array(source.buffer),
    );
  } else {
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, source as TexImageSource);
  }

  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
};

export { getSourceSize, isAndroidMiniprogram, getCanvas2D as createCanvas2D, releaseCanvas2D as releaseNativeImage };
