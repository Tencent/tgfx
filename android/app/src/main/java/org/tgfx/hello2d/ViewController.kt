/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector

class ViewController(private val view: TGFXView) {
    private var lastPan = PointF(0f, 0f)
    private var lastOffset = PointF(0f, 0f)
    private var isScaling = false
    private var needResetPanAfterScale = false

    private val scaleGestureDetector = ScaleGestureDetector(
        view.context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                isScaling = true
                return true
            }
            override fun onScaleEnd(detector: ScaleGestureDetector) {
                android.util.Log.d("ViewController", "onScaleEnd")
                isScaling = false
                needResetPanAfterScale = true
            }
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                val oldZoom = view.zoomScale
                val newZoom = (oldZoom * detector.scaleFactor).coerceIn(0.001f, 1000.0f)
                val focusX = detector.focusX
                val focusY = detector.focusY
                val contentX = (focusX - view.contentOffset.x) / oldZoom
                val contentY = (focusY - view.contentOffset.y) / oldZoom
                view.zoomScale = newZoom
                view.contentOffset = PointF(
                    focusX - contentX * newZoom,
                    focusY - contentY * newZoom
                )
                lastOffset.set(view.contentOffset.x, view.contentOffset.y)
                view.draw(view.drawIndex)
                return true
            }
        }
    )

    private val gestureDetector = GestureDetector(
        view.context,
        object : GestureDetector.SimpleOnGestureListener() {
            override fun onDown(e: MotionEvent): Boolean {
                lastPan.set(e.x, e.y)
                lastOffset.set(view.contentOffset.x, view.contentOffset.y)
                return true
            }
            override fun onSingleTapUp(e: MotionEvent): Boolean {
                if (!isScaling) {
                    view.performClick()
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
                    lastOffset.set(view.contentOffset.x, view.contentOffset.y)
                    needResetPanAfterScale = false
                    return false
                }
                val dx = e2.x - lastPan.x
                val dy = e2.y - lastPan.y
                view.contentOffset = PointF(
                    lastOffset.x + dx,
                    lastOffset.y + dy
                )
                view.draw(view.drawIndex)
                return true
            }
        }
    )

    fun onTouchEvent(event: MotionEvent): Boolean {
        if (
            needResetPanAfterScale &&
            !isScaling &&
            event.pointerCount == 1 &&
            event.actionMasked == MotionEvent.ACTION_MOVE
        ) {
            lastPan.set(event.x, event.y)
            lastOffset.set(view.contentOffset.x, view.contentOffset.y)
            needResetPanAfterScale = false
        }
        var handled = scaleGestureDetector.onTouchEvent(event)
        if (!isScaling) {
            handled = gestureDetector.onTouchEvent(event) || handled
        }
        return handled
    }
}