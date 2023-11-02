package io.pag.tgfxdemo

import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView

open class TGFXView : TextureView, TextureView.SurfaceTextureListener {
    constructor(context: android.content.Context) : super(context) {
        setupSurfaceTexture()
    }
    constructor(context: android.content.Context, attrs: android.util.AttributeSet) : super(context, attrs) {
        setupSurfaceTexture()
    }
    constructor(context: android.content.Context, attrs: android.util.AttributeSet, defStyleAttr: Int) : super(context, attrs, defStyleAttr) {
        setupSurfaceTexture()
    }

    private fun setupSurfaceTexture() {
        surfaceTextureListener = this
    }

    override fun onSurfaceTextureAvailable(p0: SurfaceTexture, p1: Int, p2: Int) {
        release()
        surface = Surface(p0)
        nativePtr = setupFromSurface(surface!!)
        draw()
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

    fun draw() {
        nativeDraw()
    }

    private external fun updateSize()

    private external fun nativeRelease()

    private external fun nativeDraw()

    private var surface : Surface? = null

    private var nativePtr : Long = 0

    companion object {
        private external fun nativeInit()

        private external fun setupFromSurface(surface: Surface): Long

        init {
            System.loadLibrary("tgfxdemo")
            nativeInit()
        }
    }
}
