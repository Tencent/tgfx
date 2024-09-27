/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/layers/LayerType.h"

namespace tgfx {
/**
 * The base class for all layers that can be placed on the display list. The layer class includes
 * features for positioning, visibility, and alpha support, as well as methods for adding and
 * removing child layers.
 */
class Layer {
 public:
  /**
   * Creates a new Layer instance.
   */
  static std::shared_ptr<Layer> Make();

  virtual ~Layer() = default;

  /**
   * Returns the type of the layer.
   */
  virtual LayerType type() const {
    return LayerType::Generic;
  }

  /**
   * Returns the instance name of the layer. The layer can be identified in the child list of its
   * parent by calling the getChildByName() method.
   */
  const std::string& name() const {
    return _name;
  }

  /**
   * Sets the instance name of the layer.
   */
  void setName(const std::string& value) {
    _name = value;
  }

  /**
   * Returns the alpha transparency value of the layer. Valid values are 0 (fully transparent) to
   * 1 (fully opaque). The default value is 1.
   */
  float alpha() const {
    return _alpha;
  }

  /**
   * Sets the alpha transparency of the layer.
   */
  void setAlpha(float value);

  /**
   * Returns the blend mode used to composite the layer with the layers below it. The default value
   * is BlendMode::SrcOver.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Sets the blend mode of the layer.
   */
  void setBlendMode(BlendMode value);

  /**
   * Returns the position of the layer relative to the local coordinates of the parent layer.
   */
  Point position() const {
    return {_matrix.getTranslateX(), _matrix.getTranslateY()};
  }

  /**
   * Sets the position of the layer.
   */
  void setPosition(const Point& value);

  /**
   * Returns the transformation matrix applied to the layer.
   */
  const Matrix& matrix() const {
    return _matrix;
  }

  /**
   * Sets the transformation matrix applied to the layer.
   */
  void setMatrix(const Matrix& value);

  /**
   * Returns whether the layer is visible. The default value is true.
   */
  bool visible() const {
    return _visible;
  }

  /**
   * Sets the visibility of the layer.
   */
  void setVisible(bool value);

  /**
   * If set to true, the runtime caches an internal bitmap representation of the layer, including
   * its child layers. This caching can improve performance for layers with complex vector content.
   * The cacheAsBitmap is automatically set to true when you apply a filter to a layer. If a layer
   * has a filter applied, cacheAsBitmap is reported as true, even if you set it to false. When you
   * clear all filters for a layer, the cacheAsBitmap setting reverts to its last set value. A layer
   * might not create the bitmap cache if the memory exceeds the upper limit, even if you set it to
   * true. The cacheAsBitmap is best used with layers that have mostly static content and do not
   * scale frequently.
   */
  bool cacheAsBitmap() const {
    return _cacheAsBitmap || !_filters.empty();
  }

  /**
   * Sets whether the layer should be cached as a bitmap.
   */
  void setCacheAsBitmap(bool value);

  /**
   * Returns the list of filters applied to the layer.
   */
  const std::vector<std::shared_ptr<ImageFilter>>& filters() const {
    return _filters;
  }

  /**
   * Sets the list of filters applied to the layer.
   */
  void setFilters(std::vector<std::shared_ptr<ImageFilter>> value);

  /**
   * Returns the layer used as a mask for the calling layer. For masking to work (allowing scaling
   * or moving), the mask must be in an active part of the display list. However, the mask layer
   * itself will not be drawn. When layers are cached by setting the cacheAsBitmap property to true,
   * both the mask and the layer being masked must be part of the same cached bitmap. If the layer
   * is cached, the mask must be a child of the layer. If an ancestor of the display object on the
   * display list is cached, the mask must be a child of that ancestor or one of its descendants.
   * If more than one ancestor of the masked layer is cached, the mask must be a descendant of the
   * cached container closest to the masked layer in the display list. Note: A single mask object
   * cannot be used to mask more than one layer. When the mask is assigned to a second layer, it is
   * removed as the mask of the first object, and that object's mask property becomes nullptr.
   */
  std::shared_ptr<Layer> mask() const {
    return _mask;
  }

  /**
   * Sets the layer used as a mask for the calling layer.
   */
  void setMask(std::shared_ptr<Layer> value);

  /**
   * Returns the scroll rectangle bounds of the layer. The layer is cropped to the size defined by
   * the rectangle, and it scrolls within the rectangle when you change the x and y properties of
   * the scrollRect. The default value is nullptr, meaning the layer is displayed in its entirety.
   */
  const Rect* scrollRect() const {
    return _scrollRect.get();
  }

  /**
   * Sets the scroll rectangle bounds of the layer. The scrollRect value is copied internally, so
   * changes to it after calling this function have no effect.
   */
  void setScrollRect(const Rect* rect);

  /**
   * Returns the root layer of the calling layer. A DisplayList has only one root layer. If a layer
   * is not added to a display list, its root property is set to nullptr.
   */
  Layer* root() const {
    return _root;
  }

  /**
   * Returns the parent layer that contains the calling layer.
   */
  std::shared_ptr<Layer> parent() const {
    return _parent ? _parent->weakThis.lock() : nullptr;
  }

  /**
   * Returns the list of child layers that are direct children of the calling layer. Note: Do not
   * iterate through this list directly with a loop while modifying it, as the loop may skip
   * children. Instead, make a copy of the list and iterate through the copy.
   */
  const std::vector<std::shared_ptr<Layer>>& children() const {
    return _children;
  }

  /**
   * Adds a child layer to the calling layer, placing it at the top of all other children. To add a
   * child at a specific position, use the addChildAt() method. If the child layer already has a
   * different parent, it will be removed from that parent first.
   * @param child The layer to add as a child of the calling layer.
   */
  void addChild(std::shared_ptr<Layer> child);

  /**
   * Adds a child layer to the calling layer at the specified index position. An index of 0 places
   * the child at the back (bottom) of the child list for the calling layer. If the child layer
   * already has a different parent, it will be removed from that parent first.
   * @param child The layer to add as a child of the calling layer.
   * @param index The index position to which the child is added. If you specify a currently
   * occupied index position, the child layer that exists at that position and all higher positions
   * are moved up one position in the child list.
   */
  void addChildAt(std::shared_ptr<Layer> child, int index);

  /**
   * Checks if the specified layer is a child of the calling layer or the calling layer itself. The
   * search includes the entire display list, including the calling layer. This method returns true
   * for grandchildren, great-grandchildren, and so on.
   * @param child The child layer to test.
   * @return true, if the child layer is a child of the calling layer or the calling layer itself,
   * otherwise false.
   */
  bool contains(std::shared_ptr<Layer> child) const;

  /**
   * Returns the child layer that exists with the specified name. If more than one child layer has
   * the specified name, the method returns the first object in the child list.
   * @param name The name of the child to return.
   * @return The first child layer with the specified name.
   */
  std::shared_ptr<Layer> getChildByName(const std::string& name);

  /**
   * Returns the index position of a child layer.
   * @param child The child layer to identify.
   * @return The index position of the child layer to identify.
   */
  int getChildIndex(std::shared_ptr<Layer> child) const;

  /**
   * Returns an array of layers that lie under the specified point and are children (or grandchildren,
   * and so on) of the calling layer. The point parameter is in the root layer's coordinate space,
   * not the parent layer's (unless the parent layer is the root). You can use the globalToLocal()
   * and the localToGlobal() methods to convert points between these coordinate spaces.
   * @param x The x coordinate under which to look.
   * @param y The y coordinate under which to look.
   * @return An array of layers that lie under the specified point and are children (or
   * grandchildren, and so on) of the calling layer.
   */
  std::vector<std::shared_ptr<Layer>> getLayersUnderPoint(float x, float y);

  /**
   * Removes the layer from its parent layer. If the layer is not a child of any layer, this method
   * has no effect.
   */
  void removeFromParent();

  /**
   * Removes a child layer from the specified index position in the child list of the calling layer.
   * The parent property of the removed child is set to nullptr. The index positions of any layers
   * above the child in the calling layer are decreased by 1.
   * @param index The child index of the layer to remove.
   * @return The layer that was removed.
   */
  std::shared_ptr<Layer> removeChildAt(int index);

  /**
   * Removes all child layers from the child list of the calling layer. The parent property of the
   * removed children is set to nullptr.
   * @param beginIndex The beginning position.
   * @param endIndex The ending position (included).
   */
  void removeChildren(int beginIndex = 0, int endIndex = 0x7fffffff);

  /**
   * Changes the position of an existing child within the calling layer, affecting the order of
   * child layers. When you use the setChildIndex() method and specify an index that is already
   * occupied, only the positions between the child's old and new positions will change. All other
   * positions remain the same. If a child is moved to a lower index, all children between the old
   * and new positions will have their index increased by 1. If a child is moved to a higher index,
   * all children between the old and new positions will have their index decreased by 1.
   * @param child The child layer to reposition.
   * @param index The new index position for the child layer.
   */
  void setChildIndex(std::shared_ptr<Layer> child, int index);

  /**
   * Replaces the specified child layer of the calling layer with a different layer.
   * @param oldChild The layer to be replaced.
   * @param newChild The layer with which to replace oldLayer.
   */
  void replaceChild(std::shared_ptr<Layer> oldChild, std::shared_ptr<Layer> newChild);

  /**
   * Returns a rectangle that defines the area of the layer relative to the coordinates of the
   * targetCoordinateSpace layer. If the targetCoordinateSpace is nullptr, this method returns a
   * rectangle that defines the area of the layer relative to its own coordinates.
   * @param targetCoordinateSpace The layer that defines the coordinate system to use.
   * @return The rectangle that defines the area of the layer relative to the targetCoordinateSpace
   * layer's coordinate system.
   */
  Rect getBounds(const Layer* targetCoordinateSpace = nullptr) const;

  /**
   * Converts the point from the root's (global) coordinates to the layer's (local) coordinates.
   * @param globalPoint A point with coordinates relative to the root layer.
   * @return A Point with coordinates relative to the calling layer.
   */
  Point globalToLocal(const Point& globalPoint) const;

  /**
   * Converts the point from the layer's (local) coordinates to the root's (global) coordinates.
   * @param localPoint A point with coordinates relative to the calling layer.
   * @return A Point with coordinates relative to the root layer.
   */
  Point localToGlobal(const Point& localPoint) const;

  /**
   * Checks if the layer overlaps or intersects with the specified point (x, y).
   * The x and y coordinates are in the root layer's coordinate space, not the parent layer's
   * (unless the parent layer is the root). You can use the globalToLocal() and the localToGlobal()
   * methods to convert points between these coordinate spaces.
   * @param x The x coordinate to test it against the calling layer.
   * @param y The y coordinate to test it against the calling layer.
   * @param shapeFlag Whether to check against the actual pixels of the layer (true) or just the
   * bounding box (false).
   * @return true if the layer overlaps or intersects with the specified point, false otherwise.
   */
  bool hitTestPoint(float x, float y, bool shapeFlag = false);

 protected:
  std::weak_ptr<Layer> weakThis;

  Layer() = default;

  /**
   * Called when the layer needs to be redrawn.
   */
  void invalidate();

 private:
  bool dirty = true;
  std::string _name;
  float _alpha = 1.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
  Matrix _matrix = Matrix::I();
  bool _visible = true;
  bool _cacheAsBitmap = false;
  std::vector<std::shared_ptr<ImageFilter>> _filters = {};
  std::shared_ptr<Layer> _mask = nullptr;
  std::unique_ptr<Rect> _scrollRect = nullptr;
  Layer* _root = nullptr;
  Layer* _parent = nullptr;
  std::vector<std::shared_ptr<Layer>> _children = {};
};
}  // namespace tgfx
