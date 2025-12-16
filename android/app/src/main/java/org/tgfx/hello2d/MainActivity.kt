package org.tgfx.hello2d

import android.animation.ValueAnimator
import android.graphics.PointF
import android.os.Bundle
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import androidx.activity.ComponentActivity


class MainActivity : ComponentActivity() {

    private lateinit var tgfxView: TGFXView
    private var drawIndex = 0
    private var zoomScale = 1.0f
    private var contentOffset = PointF(0f, 0f)
    private var animator: ValueAnimator? = null
    private lateinit var scaleGestureDetector: ScaleGestureDetector
    private lateinit var gestureDetector: GestureDetector

    companion object {
        private const val MAX_ZOOM = 1000.0f
        private const val MIN_ZOOM = 0.001f
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        tgfxView = findViewById<TGFXView>(R.id.tgfx_view)
        setupGesture()
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            repeatCount = ValueAnimator.INFINITE
            addUpdateListener {
                tgfxView.draw()
            }
            start()
        }
    }

    override fun onPause() {
        super.onPause()
        animator?.pause()
    }

    override fun onResume() {
        super.onResume()
        animator?.resume()
    }

    override fun onDestroy() {
        super.onDestroy()
        animator?.cancel()
        animator = null
    }

    private fun setupGesture() {
        scaleGestureDetector = ScaleGestureDetector(this,
            object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
                override fun onScale(detector: ScaleGestureDetector): Boolean {
                    val oldZoom = zoomScale
                    zoomScale = (zoomScale * detector.scaleFactor).coerceIn(MIN_ZOOM, MAX_ZOOM)
                    contentOffset.x = detector.focusX - (detector.focusX - contentOffset.x) * zoomScale / oldZoom
                    contentOffset.y = detector.focusY - (detector.focusY - contentOffset.y) * zoomScale / oldZoom
                    tgfxView.updateDisplayTransform(zoomScale, contentOffset)
                    return true
                }
            })

        gestureDetector = GestureDetector(this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onScroll(
                    e1: MotionEvent?,
                    e2: MotionEvent,
                    distanceX: Float,
                    distanceY: Float
                ): Boolean {
                    contentOffset.x -= distanceX
                    contentOffset.y -= distanceY
                    tgfxView.updateDisplayTransform(zoomScale, contentOffset)
                    return true
                }

                override fun onSingleTapUp(e: MotionEvent): Boolean {
                    drawIndex++
                    zoomScale = 1.0f
                    contentOffset.set(0f, 0f)
                    tgfxView.updateDisplayList(drawIndex)
                    tgfxView.updateDisplayTransform(zoomScale, contentOffset)
                    return true
                }
            })
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        scaleGestureDetector.onTouchEvent(event)
        gestureDetector.onTouchEvent(event)
        return true
    }
}
