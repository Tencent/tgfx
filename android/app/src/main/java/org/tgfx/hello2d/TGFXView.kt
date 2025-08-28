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

package org.tgfx.hello2d

import android.graphics.PointF
import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView
import java.io.InputStream

open class TGFXView : TextureView, TextureView.SurfaceTextureListener {


    constructor(context: android.content.Context) : super(context) {
        setupSurfaceTexture()
    }

    constructor(context: android.content.Context, attrs: android.util.AttributeSet) : super(
        context,
        attrs
    ) {
        setupSurfaceTexture()
    }

    constructor(
        context: android.content.Context,
        attrs: android.util.AttributeSet,
        defStyleAttr: Int
    ) : super(context, attrs, defStyleAttr) {
        setupSurfaceTexture()
    }


    private fun setupSurfaceTexture() {
        surfaceTextureListener = this
    }


    override fun onSurfaceTextureAvailable(
        surfaceTexture: SurfaceTexture,
        width: Int,
        height: Int
    ) {
        release()

        val streamBridge: InputStream = context.assets.open("bridge.jpg")
        val imageBytesBridge = ByteArray(streamBridge.available())
        streamBridge.read(imageBytesBridge)
        streamBridge.close()

        val streamTGFX: InputStream = context.assets.open("tgfx.png")
        val imageBytesTGFX = ByteArray(streamTGFX.available())
        streamTGFX.read(imageBytesTGFX)
        streamTGFX.close()

        val streamFont: InputStream = context.assets.open("NotoColorEmoji.ttf")
        val fontBytes = ByteArray(streamFont.available())
        streamFont.read(fontBytes)
        streamFont.close()

        val metrics = resources.displayMetrics

        surface = Surface(surfaceTexture)
        val imageBytesArray = arrayOf(imageBytesBridge, imageBytesTGFX)
        nativePtr = setupFromSurface(
            surface!!,
            imageBytesArray,
            fontBytes,
            metrics.density
        )

        draw(0, 1.0f, PointF(0f, 0f))
    }


    override fun onSurfaceTextureSizeChanged(
        surfaceTexture: SurfaceTexture,
        width: Int,
        height: Int
    ) {
        updateSize()
    }

    override fun onSurfaceTextureDestroyed(surfaceTexture: SurfaceTexture): Boolean {
        return true
    }


    override fun onSurfaceTextureUpdated(surfaceTexture: SurfaceTexture) {
    }


    fun release() {
        surface?.let {
            it.release()
            surface = null
        }
        nativeRelease()
    }


    fun draw(index: Int, zoom: Float, offset: PointF): Boolean {
        return nativeDraw(index, zoom, offset.x, offset.y)
    }


    private external fun updateSize()

    private external fun nativeRelease()

    private external fun nativeDraw(
        drawIndex: Int,
        zoom: Float,
        offsetX: Float,
        offsetY: Float
    ): Boolean


    private var surface: Surface? = null

    private var nativePtr: Long = 0


    companion object {

        private external fun nativeInit()


        private external fun setupFromSurface(
            surface: Surface,
            imageBytes: Array<ByteArray>,
            fontBytes: ByteArray,
            density: Float
        ): Long

        init {
            System.loadLibrary("hello2d")
            nativeInit()
        }
    }
}
