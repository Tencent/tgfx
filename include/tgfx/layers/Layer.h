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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/layers/LayerContent.h"
#include "tgfx/layers/LayerType.h"
#include "tgfx/layers/filters/LayerFilter.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

class DisplayList;
class DrawArgs;
struct LayerStyleSource;

/**
 * The base class for all layers that can be placed on the display list. The layer class includes
 * features for positioning, visibility, and alpha support, as well as methods for adding and
 * removing child layers. Note that all layers are not thread-safe and should be accessed from a
 * single thread. Some properties only take effect if the layer has a parent, such as alpha,
 * blendMode, position, matrix, visible, scrollRect, and mask.
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

  virtual ~Layer();

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
   * Returns the list of layer styles applied to the layer. Unlike layer filters, layer styles do
   * not create a new offscreen image to replace the original layer content. Instead, they add
   * visual elements either below or above the layer content, blending directly with the existing
   * content on the canvas. Each layer style uses the same layer content as input and draws on the
   * canvas. Layer styles are applied before filters. The default value is an empty list.
   */
  const std::vector<std::shared_ptr<LayerStyle>>& layerStyles() const {
    return _layerStyles;
  }

  /**
   * Sets the list of layer styles applied to the layer.
   */
  void setLayerStyles(const std::vector<std::shared_ptr<LayerStyle>>& value);

  /**
   * Whether to exclude child effects in the layer style. If true, child layer
   * styles and filters are not included in the layer content used to generate
   * the layer style. This option only affects the appearance of the LayerStyle, not the layer
   * itself. The default value is false.
   */
  bool excludeChildEffectsInLayerStyle() const {
    return bitFields.excludeChildEffectsInLayerStyle;
  }

  /**
   * Sets whether exclude child effects in the layer style.
   */
  void setExcludeChildEffectsInLayerStyle(bool value);

  /**
   * Returns the list of filters applied to the layer. Layer filters create new offscreen images
   * to replace the original layer content. Each filter takes the output of the previous filter as
   * input, and the final output is drawn on the canvas. Layer filters are applied after layer
   * styles. The default value is an empty list.
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
   * itself will not be drawn. Note: A single mask object cannot be used to mask more than one
   * layer. When the mask is assigned to a second layer, it is removed as the mask of the first
   * object, and that object's mask property becomes nullptr.
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
   * @param pixelHitTest Whether to check the actual pixels of the layer (true) or just the bounding
   * box (false). Note that Image layers are always checked against their bounding box. You can draw
   * image layers to a Surface and use the Surface::getColor() method to check the actual pixels.
   * @return true if the layer overlaps or intersects with the specified point, false otherwise.
   */
  bool hitTestPoint(float x, float y, bool pixelHitTest = false);

  /**
   * Draws the layer and all its children onto the given canvas. You can specify the alpha and blend
   * mode to control how the layer is drawn. Note: The layer is drawn in its local space without
   * applying its own matrix, alpha, blend mode, visible, scrollRect, or mask.
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
  void invalidateTransform();

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

  /**
   * Draws the layer content and its children on the given canvas. By default, this method draws the
   * layer content first, followed by the children. Subclasses can override this method to change
   * the drawing order or the way the layer content is drawn.
   * @param content The layer content to draw. This can be nullptr.
   * @param canvas The canvas to draw the layer content on.
   * @param alpha The alpha transparency value used for drawing the layer content.
   * @param forContour Whether to draw the layer content for the contour.
   * @param drawChildren A callback function that draws the children of the layer. if the function
   * return false, the content above children should not be drawn.
   */
  virtual void drawContents(LayerContent* content, Canvas* canvas, float alpha, bool forContour,
                            const std::function<bool()>& drawChildren) const;

  /**
  * Attachs a property to this layer.
  */
  void attachProperty(LayerProperty* property) const;

  /**
   * Detaches a property from this layer.
   */
  void detachProperty(LayerProperty* property) const;

 private:
  /**
   * Marks the layer's children as changed and needing to be redrawn.
   */
  void invalidateChildren();

  void onAttachToRoot(Layer* owner);

  void onDetachFromRoot();

  int doGetChildIndex(const Layer* child) const;

  bool doContains(const Layer* child) const;

  Matrix getGlobalMatrix() const;

  Matrix getMatrixWithScrollRect() const;

  LayerContent* getContent();

  Paint getLayerPaint(float alpha, BlendMode blendMode = BlendMode::SrcOver) const;

  std::shared_ptr<ImageFilter> getImageFilter(float contentScale);

  LayerContent* getRasterizedCache(const DrawArgs& args);

  std::shared_ptr<Image> getRasterizedImage(const DrawArgs& args, float contentScale,
                                            Matrix* drawingMatrix);

  void drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode);

  void drawOffscreen(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode);

  void drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha);

  bool drawChildren(const DrawArgs& args, Canvas* canvas, float alpha, Layer* stopChild = nullptr);

  void drawBackground(const DrawArgs& args, Canvas* canvas, float* contentAlpha = nullptr);

  std::unique_ptr<LayerStyleSource> getLayerStyleSource(const DrawArgs& args, const Matrix& matrix);

  void drawLayerStyles(Canvas* canvas, float alpha, const LayerStyleSource* source,
                       LayerStylePosition position);

  bool getLayersUnderPointInternal(float x, float y, std::vector<std::shared_ptr<Layer>>* results);

  std::shared_ptr<MaskFilter> getMaskFilter(const DrawArgs& args, float scale);

  Matrix getRelativeMatrix(const Layer* targetCoordinateSpace) const;

  bool hasValidMask() const;

  void invalidate();

  struct {
    bool dirtyContent : 1;      // need to update content
    bool dirtyDescendents : 1;  // need to redraw child layers
    bool dirtyTransform : 1;    // need to redraw the layer, property such as alpha,
                                // blendMode, matrix, etc.
    bool visible : 1;
    bool shouldRasterize : 1;
    bool allowsEdgeAntialiasing : 1;
    bool allowsGroupOpacity : 1;
    bool excludeChildEffectsInLayerStyle : 1;
  } bitFields = {};
  std::string _name;
  float _alpha = 1.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
  Matrix _matrix = Matrix::I();
  float _rasterizationScale = 1.0f;
  std::vector<std::shared_ptr<LayerFilter>> _filters = {};
  std::shared_ptr<Layer> _mask = nullptr;
  Layer* maskOwner = nullptr;
  std::unique_ptr<Rect> _scrollRect = nullptr;
  Layer* _root = nullptr;
  Layer* _parent = nullptr;
  std::unique_ptr<LayerContent> layerContent = nullptr;
  std::unique_ptr<LayerContent> rasterizedContent = nullptr;
  std::vector<std::shared_ptr<Layer>> _children = {};
  std::vector<std::shared_ptr<LayerStyle>> _layerStyles = {};

  friend class DisplayList;
  friend class LayerProperty;
};
}  // namespace tgfx
