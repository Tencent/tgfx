/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

package org.tgfx.test;

import android.graphics.SurfaceTexture;

/** Injects JNI failures before a real image latch or in the timestamp query after that latch. */
public final class RetrySurfaceTexture extends SurfaceTexture {
    public int failuresRemaining = 0;
    public int latchCalls = 0;
    public int timestampFailuresRemaining = 0;

    public RetrySurfaceTexture() {
        super(false);
    }

    @Override
    public void updateTexImage() {
        latchCalls++;
        if (failuresRemaining > 0) {
            failuresRemaining--;
            throw new IllegalStateException("Injected failure before native latch");
        }
        super.updateTexImage();
    }

    @Override
    public long getTimestamp() {
        if (timestampFailuresRemaining > 0) {
            timestampFailuresRemaining--;
            throw new IllegalStateException("Injected timestamp failure after native latch");
        }
        return super.getTimestamp();
    }
}
