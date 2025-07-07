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
import java.io.ByteArrayOutputStream

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

    override fun onSurfaceTextureAvailable(p0: SurfaceTexture, p1: Int, p2: Int) {
        release()
        val stream: InputStream = context.assets.open("bridge.jpg")
        val imageBytes: ByteArray = ByteArray(stream.available())
        stream.read(imageBytes)
        stream.close();
        val metrics = resources.displayMetrics
        surface = Surface(p0)
        nativePtr = setupFromSurface(surface!!, imageBytes, metrics.density)
    }

    override fun onSurfaceTextureSizeChanged(p0: SurfaceTexture, p1: Int, p2: Int) {
        updateSize()
    }

    override fun onSurfaceTextureDestroyed(p0: SurfaceTexture): Boolean {
        return true
    }

    override fun onSurfaceTextureUpdated(p0: SurfaceTexture) {
    }

    fun release() {
        if (surface != null) {
            surface!!.release()
            surface = null
        }
        nativeRelease()
    }

    fun draw(index: Int, zoom: Float, offset: PointF) {
        nativeDraw(index, zoom, offset.x, offset.y)
    }

    private external fun updateSize()

    private external fun nativeRelease()

    private external fun nativeDraw(drawIndex: Int, zoom: Float, offsetX: Float, offsetY: Float)

    private var surface: Surface? = null

    private var nativePtr: Long = 0

    companion object {
        private external fun nativeInit()

        private external fun setupFromSurface(
            surface: Surface, imageBytes: ByteArray,
            density: Float
        ): Long

        init {
            System.loadLibrary("hello2d")
            nativeInit()
        }
    }
}
