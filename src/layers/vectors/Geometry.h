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

#pragma once

#include <memory>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {

enum class GeometryType {
  Shape,
  Text,
};

/**
 * Geometry is the base class for drawable elements in VectorLayer.
 */
class Geometry {
 public:
  virtual ~Geometry() = default;

  /**
   * Returns the type of this geometry.
   */
  virtual GeometryType type() const = 0;

  /**
   * Creates a copy of this geometry.
   */
  virtual std::unique_ptr<Geometry> clone() const = 0;

  /**
   * Returns the TextBlob if this geometry contains text, or nullptr otherwise.
   */
  virtual std::shared_ptr<TextBlob> getTextBlob() = 0;

  /**
   * Returns the Shape representation of this geometry. For ShapeGeometry, returns the internal
   * shape directly. For TextGeometry, this triggers a conversion from TextBlob to Shape.
   */
  virtual std::shared_ptr<Shape> getShape() = 0;
};

/**
 * ShapeGeometry wraps a Shape for rendering.
 */
class ShapeGeometry : public Geometry {
 public:
  explicit ShapeGeometry(std::shared_ptr<Shape> shape) : shape(std::move(shape)) {
  }

  GeometryType type() const override {
    return GeometryType::Shape;
  }

  std::unique_ptr<Geometry> clone() const override {
    return std::make_unique<ShapeGeometry>(shape);
  }

  std::shared_ptr<TextBlob> getTextBlob() override {
    return nullptr;
  }

  std::shared_ptr<Shape> getShape() override {
    return shape;
  }

  std::shared_ptr<Shape> shape = nullptr;
};

/**
 * TextGeometry wraps a TextBlob for rendering.
 */
class TextGeometry : public Geometry {
 public:
  explicit TextGeometry(std::shared_ptr<TextBlob> textBlob) : textBlob(std::move(textBlob)) {
  }

  GeometryType type() const override {
    return GeometryType::Text;
  }

  std::unique_ptr<Geometry> clone() const override {
    return std::make_unique<TextGeometry>(textBlob);
  }

  std::shared_ptr<TextBlob> getTextBlob() override {
    return textBlob;
  }

  std::shared_ptr<Shape> getShape() override;

  std::shared_ptr<TextBlob> textBlob = nullptr;

 private:
  std::shared_ptr<Shape> cachedShape = nullptr;
};

}  // namespace tgfx
