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

export class BitmapImage {
    public bitmap: ImageBitmap | null;

    public constructor(bitmap: ImageBitmap | null) {
        this.bitmap = bitmap;
    }

    public setBitmap(bitmap: ImageBitmap) {
        if (this.bitmap) {
            this.bitmap.close();
        }
        this.bitmap = bitmap;
    }
}
