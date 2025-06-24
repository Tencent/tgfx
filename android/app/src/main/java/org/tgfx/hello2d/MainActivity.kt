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

package org.tgfx.hello2d

import android.graphics.PointF
import android.os.Bundle
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import androidx.activity.ComponentActivity
import android.widget.FrameLayout

class MainActivity : ComponentActivity() {
    private lateinit var tgfxView: TGFXView

    private var drawIndex = 0
    private var zoomScale = 1.0f
    private var contentOffset = PointF(0f, 0f)

    private var lastPan = PointF(0f, 0f)
    private var lastOffset = PointF(0f, 0f)
    private var isScaling = false
    private var needResetPanAfterScale = false

    private lateinit var scaleGestureDetector: ScaleGestureDetector
    private lateinit var gestureDetector: GestureDetector

    companion object {
        private const val MaxZoom = 1000.0f
        private const val MinZoom = 0.001f
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        tgfxView = TGFXView(this)
        setContentView(tgfxView)
        setupGesture()
        tgfxView.post {
            draw()
        }
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
                    val oldZoom = zoomScale
                    val newZoom = (oldZoom * detector.scaleFactor).coerceIn(MinZoom, MaxZoom)
                    val focusX = detector.focusX
                    val focusY = detector.focusY
                    val contentX = (focusX - contentOffset.x) / oldZoom
                    val contentY = (focusY - contentOffset.y) / oldZoom
                    zoomScale = newZoom
                    contentOffset = PointF(
                        focusX - contentX * newZoom,
                        focusY - contentY * newZoom
                    )
                    lastOffset.set(contentOffset.x, contentOffset.y)
                    draw()
                    return true
                }
            })

        gestureDetector = GestureDetector(this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onDown(e: MotionEvent): Boolean {
                    lastPan.set(e.x, e.y)
                    lastOffset.set(contentOffset.x, contentOffset.y)
                    return true
                }

                override fun onSingleTapUp(e: MotionEvent): Boolean {
                    if (!isScaling) {
                        onSingleTap()
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
                        lastPan.set(e2.x, e2.y)
                        lastOffset.set(contentOffset.x, contentOffset.y)
                        needResetPanAfterScale = false
                        return false
                    }
                    val dx = e2.x - lastPan.x
                    val dy = e2.y - lastPan.y
                    contentOffset = PointF(
                        lastOffset.x + dx,
                        lastOffset.y + dy
                    )
                    draw()
                    return true
                }
            })
    }

    private fun onSingleTap() {
        drawIndex++
        zoomScale = 1.0f
        contentOffset.set(0f, 0f)
        draw()
    }

    private fun draw() {
        tgfxView.draw(drawIndex, zoomScale, contentOffset)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (
            needResetPanAfterScale &&
            !isScaling &&
            event.pointerCount == 1 &&
            event.actionMasked == MotionEvent.ACTION_MOVE
        ) {
            lastPan.set(event.x, event.y)
            lastOffset.set(contentOffset.x, contentOffset.y)
            needResetPanAfterScale = false
        }
        var handled = scaleGestureDetector.onTouchEvent(event)
        if (!isScaling) {
            handled = gestureDetector.onTouchEvent(event) || handled
        }
        return handled || super.onTouchEvent(event)
    }
}
