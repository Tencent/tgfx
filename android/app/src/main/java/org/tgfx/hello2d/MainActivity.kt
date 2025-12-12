package org.tgfx.hello2d

import android.animation.ValueAnimator
import android.graphics.PointF
import android.os.Bundle
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import androidx.activity.ComponentActivity
import androidx.core.view.MotionEventCompat
import kotlin.math.max
import kotlin.math.min


class MainActivity : ComponentActivity() {

    private lateinit var tgfxView: TGFXView

    private var drawIndex = 0

    private var zoomScale = 1.0f

    private var contentOffset = PointF(0f, 0f)

    private lateinit var animator: ValueAnimator

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

    }

    override fun onPause() {
        super.onPause()
        if (::animator.isInitialized) {
            animator.cancel()
        }
    }


    override fun onResume() {
        super.onResume()
        requestDraw()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (::animator.isInitialized) {
            animator.cancel()
        }
    }


    private fun setupGesture() {
        scaleGestureDetector = ScaleGestureDetector(this,
            object : ScaleGestureDetector.SimpleOnScaleGestureListener() {


                override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                    val focusX = detector.focusX
                    val focusY = detector.focusY
                    return true
                }

                override fun onScale(detector: ScaleGestureDetector): Boolean {
                    val oldZoom = zoomScale
                    zoomScale = (zoomScale * detector.scaleFactor).coerceIn(MIN_ZOOM, MAX_ZOOM)

                    contentOffset.x = detector.focusX - (detector.focusX - contentOffset.x) * zoomScale / oldZoom
                    contentOffset.y = detector.focusY - (detector.focusY - contentOffset.y) * zoomScale / oldZoom

                    requestDraw()
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
                    requestDraw()
                    return true
                }


                override fun onSingleTapUp(e: MotionEvent): Boolean {
                    drawIndex = (drawIndex + 1)
                    zoomScale = 1.0f
                    contentOffset.set(0f, 0f)
                    requestDraw()
                    return true
                }
            })
    }


    override fun onTouchEvent(event: MotionEvent): Boolean {
        scaleGestureDetector.onTouchEvent(event)
        gestureDetector.onTouchEvent(event)
        return true
    }


    private fun requestDraw() {
        if (::animator.isInitialized && animator.isRunning) {
            return
        }

        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 1000
            addUpdateListener {
                try {
                    val needsRedraw = tgfxView.draw(drawIndex, zoomScale, contentOffset)
                    if (!needsRedraw) {
                        cancel()
                    }
                } catch (e: Exception) {
                    cancel()
                }
            }
            start()
        }
    }

}
