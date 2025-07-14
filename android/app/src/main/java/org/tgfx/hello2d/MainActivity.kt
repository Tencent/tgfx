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

    private var currentPan = PointF(0f, 0f)
    private var currentOffset = PointF(0f, 0f)
    private var isScaling = false
    private var needResetPanAfterScale = false

    private lateinit var scaleGestureDetector: ScaleGestureDetector
    private lateinit var gestureDetector: GestureDetector

    private var animator: ValueAnimator? = null

    companion object {
        private const val MaxZoom = 1000.0f
        private const val MinZoom = 0.001f
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        tgfxView = findViewById<TGFXView>(R.id.tgfx_view)
        setupGesture()

        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            repeatCount = ValueAnimator.INFINITE
            addUpdateListener { animation ->
                tgfxView.draw(drawIndex, zoomScale, contentOffset)
            }
            start()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        animator?.cancel()
    }

    private fun setupGesture() {
        scaleGestureDetector = ScaleGestureDetector(this,
            object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
                override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                    isScaling = true
                    return true
                }

                override fun onScaleEnd(detector: ScaleGestureDetector) {
                    isScaling = false
                    needResetPanAfterScale = true
                }

                override fun onScale(detector: ScaleGestureDetector): Boolean {
                    val contentX = (detector.focusX - contentOffset.x) / zoomScale
                    val contentY = (detector.focusY - contentOffset.y) / zoomScale
                    zoomScale = (zoomScale * detector.scaleFactor).coerceIn(MinZoom, MaxZoom)
                    contentOffset = PointF(
                        detector.focusX - contentX * zoomScale,
                        detector.focusY - contentY * zoomScale
                    )
                    currentOffset.set(contentOffset.x, contentOffset.y)
                    return true
                }
            })

        gestureDetector = GestureDetector(this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onDown(e: MotionEvent): Boolean {
                    currentPan.set(e.x, e.y)
                    currentOffset.set(contentOffset.x, contentOffset.y)
                    return true
                }

                override fun onSingleTapUp(e: MotionEvent): Boolean {
                    if (!isScaling) {
                        drawIndex++
                        zoomScale = 1.0f
                        contentOffset.set(0f, 0f)
                        return true
                    }
                    return false
                }

                override fun onScroll(
                    e1: MotionEvent?,
                    e2: MotionEvent,
                    distanceX: Float,
                    distanceY: Float
                ): Boolean {
                    if (needResetPanAfterScale) {
                        currentPan.set(e2.x, e2.y)
                        currentOffset.set(contentOffset.x, contentOffset.y)
                        needResetPanAfterScale = false
                        return false
                    }
                    contentOffset = PointF(
                        currentOffset.x + e2.x - currentPan.x,
                        currentOffset.y + e2.y - currentPan.y
                    )
                    return true
                }
            })
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (
            needResetPanAfterScale &&
            !isScaling &&
            event.pointerCount == 1 &&
            event.actionMasked == MotionEvent.ACTION_MOVE
        ) {
            currentPan.set(event.x, event.y)
            currentOffset.set(contentOffset.x, contentOffset.y)
            needResetPanAfterScale = false
        }
        var handled = scaleGestureDetector.onTouchEvent(event)
        if (!isScaling) {
            handled = gestureDetector.onTouchEvent(event) || handled
        }
        return handled || super.onTouchEvent(event)
    }
}
