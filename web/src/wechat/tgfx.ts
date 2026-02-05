/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

import { getSourceSize, isAndroidMiniprogram ,setColorSpace,hasWebpSupport} from '../tgfx';
import { ArrayBufferImage } from './array-buffer-image';
import { getCanvas2D, releaseCanvas2D } from './canvas';

import {EmscriptenGL, TGFX} from '../types';

type WxOffscreenCanvas = OffscreenCanvas & { isOffscreenCanvas: boolean };

export const uploadToTexture = (
  GL: EmscriptenGL,
  source: TexImageSource | ArrayBufferImage | WxOffscreenCanvas,
  textureID: number,
  alphaOnly: boolean,
) => {
  if (!source) {
    return;
  }
  const gl = GL.currentContext?.GLctx as WebGL2RenderingContext;
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

export const readImagePixels = (module: TGFX, image: CanvasImageSource, width: number, height: number) => {
  if (!image) {
    return null;
  }
  const canvas = getCanvas2D(width, height);
  const ctx = canvas.getContext('2d', {willReadFrequently: true}) as CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D | null;
  if (!ctx) {
    return null;
  }
  ctx.drawImage(image, 0, 0, width, height);
  const {data} = ctx.getImageData(0, 0, width, height);
  releaseCanvas2D(canvas);
  if (data.length === 0) {
    return null;
  }
  return new Uint8Array(data);
};

export { getSourceSize, isAndroidMiniprogram, getCanvas2D as createCanvas2D, releaseCanvas2D as releaseNativeImage,setColorSpace,hasWebpSupport };

