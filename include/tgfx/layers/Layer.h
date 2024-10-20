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
#include "core/utils/UniqueID.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/layers/LayerContent.h"
#include "tgfx/layers/LayerFilter.h"
#include "tgfx/layers/LayerType.h"

namespace tgfx {

class DisplayList;

/**
 * The base class for all layers that can be placed on the display list. The layer class includes
 * features for positioning, visibility, and alpha support, as well as methods for adding and
 * removing child layers. Note: All layers are not thread-safe and should be accessed from a single
 * thread.
 */
class Layer {
 public:
  /**
   * Returns the default value for the allowsEdgeAntialiasing property for new Layer instances. The
   * default value is true.
   */
  static bool DefaultAllowsEdgeAntialiasing();

  /**
   * Sets the default value for the allowsEdgeAntialiasing property for new Layer instances.
   */
  static void SetDefaultAllowsEdgeAntialiasing(bool value);

  /**
   * Returns the default value for the allowsGroupOpacity property for new Layer instances. The
   * default value is false.
   */
  static bool DefaultAllowsGroupOpacity();

  /**
   * Sets the default value for the allowsGroupOpacity property for new Layer instances.
   */
  static void SetDefaultAllowsGroupOpacity(bool value);

  /**
   * Creates a new Layer instance.
   */
  static std::shared_ptr<Layer> Make();

  virtual ~Layer() = default;

  /**
   * Returns the type of the layer.
   */
  virtual LayerType type() const {
    return LayerType::Layer;
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
    return bitFields.visible;
  }

  /**
   * Sets the visibility of the layer.
   */
  void setVisible(bool value);

  /**
   * Indicates whether the layer is cached as a bitmap before compositing. If true, the layer is
   * rendered as a bitmap in its local coordinate space and then composited with other content. Any
   * filters in the filters property are rasterized and included in the bitmap, but the current
   * alpha of the layer is not. If false, the layer is composited directly into the destination
   * whenever possible. The layer may still be rasterized before compositing if certain features
   * (like filters) require it. This caching can improve performance for layers with complex
   * content. The default value is false.
   */
  bool shouldRasterize() const {
    return bitFields.shouldRasterize;
  }

  /**
   * Sets whether the layer should be rasterized.
   */
  void setShouldRasterize(bool value);

  /**
   * The scale at which to rasterize content, relative to the coordinate space of the layer. When
   * the value in the shouldRasterize property is true, the layer uses this property to determine
   * whether to scale the rasterized content (and by how much). The default value of this property
   * is 1.0, which indicates that the layer should be rasterized at its current size. Larger values
   * magnify the content and smaller values shrink it.
   */
  float rasterizationScale() const {
    return _rasterizationScale;
  }

  /**
   * Sets the scale at which to rasterize content.
   */
  void setRasterizationScale(float value);

  /**
   * Returns true if the layer is allowed to perform edge antialiasing. This means the edges of
   * shapes and images can be drawn with partial transparency. The default value is read from the
   * Layer::DefaultAllowsEdgeAntialiasing() method.
   */
  bool allowsEdgeAntialiasing() const {
    return bitFields.allowsEdgeAntialiasing;
  }

  /**
   * Sets whether the layer is allowed to perform edge antialiasing.
   */
  void setAllowsEdgeAntialiasing(bool value);

  /**
   * Returns true if the layer is allowed to be composited as a separate group from their parent.
   * When true and the layer’s alpha value is less than 1.0, the layer can composite itself
   * separately from its parent. This ensures correct rendering for layers with multiple opaque
   * components but may reduce performance. The default value is read from the
   * Layer::DefaultAllowsGroupOpacity() method.
   */
  bool allowsGroupOpacity() const {
    return bitFields.allowsGroupOpacity;
  }

  /**
   * Sets whether the layer is allowed to be composited as a separate group from their parent.
   */
  void setAllowsGroupOpacity(bool value);

  /**
   * Returns the list of filters applied to the layer.
   */
  const std::vector<std::shared_ptr<LayerFilter>>& filters() const {
    return _filters;
  }

  /**
   * Sets the list of filters applied to the layer.
   */
  void setFilters(std::vector<std::shared_ptr<LayerFilter>> value);

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
   * the scrollRect. The properties of the scrollRect Rectangle object use the layer's coordinate
   * space and are scaled just like the overall layer. The corner bounds of the cropped viewport on
   * the scrolling layer are the origin of the layer (0,0) and the point defined by the width and
   * height of the rectangle. They are not centered around the origin, but use the origin to define
   * the upper-left corner of the area. You can scroll a layer left and right by setting the x
   * property of the scrollRect. You can scroll a layer up and down by setting the y property of the
   * scrollRect. If the layer is rotated 90° and if you scroll it left and right, the layer actually
   * scrolls up and down. The default value is an empty Rect, meaning the layer is displayed in its
   * entirety and no scrolling is applied.
   */
  Rect scrollRect() const {
    return _scrollRect ? *_scrollRect : Rect::MakeEmpty();
  }

  /**
   * Sets the scroll rectangle bounds of the layer. The scrollRect value is copied internally, so
   * changes to it after calling this function have no effect.
   */
  void setScrollRect(const Rect& rect);

  /**
   * Returns the display list owner of the calling layer. If a layer is not added to a display list,
   * its owner property is set to nullptr.
   */
  DisplayList* owner() const {
    return _owner;
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
   * @return true, if the child layer is added as a child of the calling layer, otherwise false.
   */
  bool addChild(std::shared_ptr<Layer> child);

  /**
   * Adds a child layer to the calling layer at the specified index position. An index of 0 places
   * the child at the back (bottom) of the child list for the calling layer. If the child layer
   * already has a different parent, it will be removed from that parent first.
   * @param child The layer to add as a child of the calling layer.
   * @param index The index position to which the child is added. If you specify a currently
   * occupied index position, the child layer that exists at that position and all higher positions
   * are moved up one position in the child list.
   * @return true, if the child layer is added as a child of the calling layer, otherwise false.
   */
  bool addChildAt(std::shared_ptr<Layer> child, int index);

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
   * @return true, if the child layer is repositioned, otherwise false.
   */
  bool setChildIndex(std::shared_ptr<Layer> child, int index);

  /**
   * Replaces the specified child layer of the calling layer with a different layer.
   * @param oldChild The layer to be replaced.
   * @param newChild The layer with which to replace oldLayer.
   * @return true, if the child layer is replaced, otherwise false.
   */
  bool replaceChild(std::shared_ptr<Layer> oldChild, std::shared_ptr<Layer> newChild);

  /**
   * Returns a rectangle that defines the area of the layer relative to the coordinates of the
   * targetCoordinateSpace layer. If the targetCoordinateSpace is nullptr, this method returns a
   * rectangle that defines the area of the layer relative to its own coordinates.
   * @param targetCoordinateSpace The layer that defines the coordinate system to use.
   * @return The rectangle that defines the area of the layer relative to the targetCoordinateSpace
   * layer's coordinate system.
   */
  Rect getBounds(const Layer* targetCoordinateSpace = nullptr);

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
   * Returns an array of layers under the specified point that are children (or descendants) of the
   * calling layer. The layers are checked against their bounding boxes for quick selection. The
   * first layer in the array is the top-most layer under the point, and the last layer is the
   * bottom-most. The point parameter is in the root layer's coordinate space, not the parent
   * layer's (unless the parent layer is the root). You can use the globalToLocal() and the
   * localToGlobal() methods to convert points between these coordinate spaces.
   * @param x The x coordinate under which to look.
   * @param y The y coordinate under which to look.
   * @return An array of layers that lie under the specified point and are children (or
   * grandchildren, and so on) of the calling layer.
   */
  std::vector<std::shared_ptr<Layer>> getLayersUnderPoint(float x, float y);

  /**
   * Checks if the layer overlaps or intersects with the specified point (x, y).
   * The x and y coordinates are in the root layer's coordinate space, not the parent layer's
   * (unless the parent layer is the root). You can use the globalToLocal() and the localToGlobal()
   * methods to convert points between these coordinate spaces.
   * @param x The x coordinate to test it against the calling layer.
   * @param y The y coordinate to test it against the calling layer.
   * @param shapeFlag Whether to check the actual pixels of the layer (true) or just the bounding
   * box (false). Note that Image layers are always checked against their bounding box. You can draw
   * image layers to a Surface and use the Surface::getColor() method to check the actual pixels.
   * @return true if the layer overlaps or intersects with the specified point, false otherwise.
   */
  bool hitTestPoint(float x, float y, bool shapeFlag = false);

  /**
   * Draws the layer and all its children onto the given canvas. You can specify the alpha and blend
   * mode to control how the layer is drawn. Note: The layer is drawn in its local space without
   * applying its own matrix, alpha, blend mode, scrollRect, or visibility.
   * @param canvas The canvas to draw the layer on.
   * @param alpha The alpha transparency value used for drawing the layer and its children.
   * @param blendMode The blend mode used to composite the layer with the existing content on the
   * canvas.
   */
  void draw(Canvas* canvas, float alpha = 1.0f, BlendMode blendMode = BlendMode::SrcOver);

 protected:
  std::weak_ptr<Layer> weakThis;

  Layer();

  /**
   * Marks the layer as needing to be redrawn. Unlike invalidateContent(), this method only marks
   * the layer as dirty and does not update the layer content.
   */
  void invalidate();

  /**
   * Marks the layer's content as changed and needing to be redrawn. The updateContent() method will
   * be called to create the new layer content.
   */
  void invalidateContent();

  /**
   * Called when the layer's content needs to be updated. Subclasses should override this method to
   * create the layer content used for measuring the bounding box and drawing the layer itself
   * (children not included).
   */
  virtual std::unique_ptr<LayerContent> onUpdateContent();

 private:
  /**
   * Marks the layer's children as changed and needing to be redrawn.
   */
  void invalidateChildren();

  void onAttachToDisplayList(DisplayList* owner);

  void onDetachFromDisplayList();

  int doGetChildIndex(const Layer* child) const;

  bool doContains(const Layer* child) const;

  Matrix getGlobalMatrix() const;

  Matrix getMatrixWithScrollRect() const;

  LayerContent* getContent();

  Paint getLayerPaint(float alpha, BlendMode blendMode);

  LayerContent* getRasterizedCache(Context* context, uint32_t drawFlags);

  void drawLayer(Canvas* canvas, float alpha, BlendMode blendMode, uint32_t drawFlags);

  void drawContents(Canvas* canvas, float alpha, uint32_t drawFlags);

  std::string _name;
  float _alpha = 1.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
  Matrix _matrix = Matrix::I();
  float _rasterizationScale = 1.0f;
  std::vector<std::shared_ptr<LayerFilter>> _filters = {};
  std::shared_ptr<Layer> _mask = nullptr;
  std::unique_ptr<Rect> _scrollRect = nullptr;
  DisplayList* _owner = nullptr;
  Layer* _parent = nullptr;
  std::unique_ptr<LayerContent> layerContent = nullptr;
  std::unique_ptr<LayerContent> rasterizedContent = nullptr;
  std::vector<std::shared_ptr<Layer>> _children = {};
  struct {
    bool dirty : 1;          // need to redraw the layer
    bool contentDirty : 1;   // need to update content
    bool childrenDirty : 1;  // need to redraw child layers
    bool visible : 1;
    bool shouldRasterize : 1;
    bool allowsEdgeAntialiasing : 1;
    bool allowsGroupOpacity : 1;
  } bitFields;

  friend class DisplayList;
};
}  // namespace tgfx
