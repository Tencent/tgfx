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
import androidx.core.view.MotionEventCompat

class MainActivity : ComponentActivity() {
    private lateinit var tgfxView: TGFXView

    private var drawIndex = 0
    private var zoomScale = 1.0f
    private var contentOffset = PointF(0f, 0f)

    private var isScaling = false
    private var activePointerId = INVALID_POINTER_ID
    private var currentTouchX = 0f
    private var currentTouchY = 0f
    private var currentFocusX = 0f
    private var currentFocusY = 0f
    private var needUpdatePanAnchor = false

    private lateinit var scaleGestureDetector: ScaleGestureDetector
    private lateinit var gestureDetector: GestureDetector

    private var animator: ValueAnimator? = null

    companion object {
        private const val MAX_ZOOM = 1000.0f
        private const val MIN_ZOOM = 0.001f
        private const val INVALID_POINTER_ID = -1
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
                override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                    isScaling = true
                    currentFocusX = detector.focusX
                    currentFocusY = detector.focusY
                    return true
                }

                override fun onScaleEnd(detector: ScaleGestureDetector) {
                    isScaling = false
                    needUpdatePanAnchor = true
                }

                override fun onScale(detector: ScaleGestureDetector): Boolean {
                    val oldZoom = zoomScale
                    zoomScale = (zoomScale * detector.scaleFactor).coerceIn(MIN_ZOOM, MAX_ZOOM)
                    contentOffset.x = detector.focusX - (detector.focusX - contentOffset.x) * zoomScale / oldZoom + detector.focusX - currentFocusX
                    contentOffset.y = detector.focusY - (detector.focusY - contentOffset.y) * zoomScale / oldZoom + detector.focusY - currentFocusY
                    currentFocusX = detector.focusX
                    currentFocusY = detector.focusY
                    return true
                }
            })

        gestureDetector = GestureDetector(this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onSingleTapUp(e: MotionEvent): Boolean {
                    if (!isScaling) {
                        drawIndex++
                        zoomScale = 1.0f
                        contentOffset.set(0f, 0f)
                        return true
                    }
                    return false
                }
            })
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        scaleGestureDetector.onTouchEvent(event)

        when (MotionEventCompat.getActionMasked(event)) {
            MotionEvent.ACTION_DOWN -> {
                currentTouchX = MotionEventCompat.getX(event, MotionEventCompat.getActionIndex(event))
                currentTouchY = MotionEventCompat.getY(event, MotionEventCompat.getActionIndex(event))
                activePointerId = MotionEventCompat.getPointerId(event, 0)
                needUpdatePanAnchor = false
            }

            MotionEvent.ACTION_MOVE -> {
                if (!isScaling && activePointerId != INVALID_POINTER_ID) {
                    MotionEventCompat.findPointerIndex(event, activePointerId).takeIf { it >= 0 }?.let { pointerIndex ->
                        if (needUpdatePanAnchor) {
                            currentTouchX = MotionEventCompat.getX(event, pointerIndex)
                            currentTouchY = MotionEventCompat.getY(event, pointerIndex)
                            needUpdatePanAnchor = false
                            return true
                        }
                        contentOffset.x += MotionEventCompat.getX(event, pointerIndex) - currentTouchX
                        contentOffset.y += MotionEventCompat.getY(event, pointerIndex) - currentTouchY
                        currentTouchX = MotionEventCompat.getX(event, pointerIndex)
                        currentTouchY = MotionEventCompat.getY(event, pointerIndex)
                    }
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                activePointerId = INVALID_POINTER_ID
                needUpdatePanAnchor = false
            }

            MotionEvent.ACTION_POINTER_UP -> {
                if (MotionEventCompat.getPointerId(event, MotionEventCompat.getActionIndex(event)) == activePointerId) {
                    (if (MotionEventCompat.getActionIndex(event) == 0) 1 else 0).takeIf { it < event.pointerCount }?.let { newPointerIndex ->
                        activePointerId = MotionEventCompat.getPointerId(event, newPointerIndex)
                        if (!isScaling) {
                            currentTouchX = MotionEventCompat.getX(event, newPointerIndex)
                            currentTouchY = MotionEventCompat.getY(event, newPointerIndex)
                            needUpdatePanAnchor = false
                        } else {
                            needUpdatePanAnchor = true
                        }
                    } ?: run {
                        activePointerId = INVALID_POINTER_ID
                        needUpdatePanAnchor = false
                    }
                }
            }

            MotionEvent.ACTION_POINTER_DOWN -> {
                if (activePointerId == INVALID_POINTER_ID) {
                    MotionEventCompat.getActionIndex(event).let { pointerIndex ->
                        currentTouchX = MotionEventCompat.getX(event, pointerIndex)
                        currentTouchY = MotionEventCompat.getY(event, pointerIndex)
                        activePointerId = MotionEventCompat.getPointerId(event, pointerIndex)
                        needUpdatePanAnchor = false
                    }
                }
            }
        }
        if (!isScaling) {
            gestureDetector.onTouchEvent(event)
        }
        return true
    }
}