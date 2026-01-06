/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include <optional>
#include <unordered_set>
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/layers/LayerMaskType.h"
#include "tgfx/layers/LayerRecorder.h"
#include "tgfx/layers/LayerType.h"
#include "tgfx/layers/filters/LayerFilter.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {
class LayerContent;
class SubtreeCache;
class DisplayList;
class DrawArgs;
class RegionTransformer;
class RootLayer;
struct LayerStyleSource;
struct MaskData;
class BackgroundContext;
enum class DrawMode;

/**
 * The base class for all layers that can be placed on the display list. The layer class includes
 * features for positioning, visibility, and alpha support, as well as methods for adding and
 * removing child layers. Note that all layers are not thread-safe and should be accessed from a
 * single thread. Some properties only take effect if the layer has a parent, such as alpha,
 * blendMode, position, matrix, visible, scrollRect, and mask.
 */
class Layer : public std::enable_shared_from_this<Layer> {
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
    return static_cast<BlendMode>(bitFields.blendMode);
  }

  /**
   * Sets the blend mode of the layer.
   */
  void setBlendMode(BlendMode value);

  /**
   * Returns true if the layer allows its background to pass through to sublayers. Note that layers
   * with non-SrcOver blend modes, filters, or 3D transforms will ignore this setting and prevent
   * background pass-through. The default value is true.
   */
  bool passThroughBackground() const {
    return bitFields.passThroughBackground;
  }

  /**
   * Sets whether the layer passes through its background to sublayers.
   */
  void setPassThroughBackground(bool value);

  /**
   * Returns the position of the layer relative to the local coordinates of the parent layer.
   */
  Point position() const;

  /**
   * Sets the position of the layer.
   */
  void setPosition(const Point& value);

  /**
   * Returns the transformation matrix applied to the layer.
   * If the matrix set via the setMatrix3D interface contains 3D transformations or projection
   * transformations, this interface will return the identity matrix; you need to use the matrix3D
   * interface to obtain the actual matrix. Otherwise, it will return the equivalent simplified
   * affine transformation matrix.
   */
  Matrix matrix() const;

  /**
   * Sets the transformation matrix applied to the layer.
   */
  void setMatrix(const Matrix& value);

  /**
   * Returns the 3D transformation matrix applied to the layer.
   */
  Matrix3D matrix3D() const {
    return _matrix3D;
  }

  /**
   * Sets the 3D transformation matrix applied to the layer.
   */
  void setMatrix3D(const Matrix3D& value);

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
  void setFilters(const std::vector<std::shared_ptr<LayerFilter>>& value);

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
   * Returns the mask type used by the layer. The mask type affects how the mask is applied to the
   * layer content, such as whether it uses the alpha channel, luminance, or contour of the
   * mask layer. The default value is LayerMaskType::Alpha.
   */
  LayerMaskType maskType() const {
    return static_cast<LayerMaskType>(bitFields.maskType);
  }

  /**
   * Sets the mask type used by the layer.
   */
  void setMaskType(LayerMaskType value);

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
  Layer* root() const;

  /**
   * Returns the parent layer that contains the calling layer.
   */
  Layer* parent() const {
    return _parent;
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
   * @param computeTightBounds Determines whether to calculate the exact tight bounding box (true)
   * or a general bounding box that may be larger but is faster to compute (false).
   * @return The rectangle that defines the area of the layer relative to the targetCoordinateSpace
   * layer's coordinate system.
   */
  Rect getBounds(const Layer* targetCoordinateSpace = nullptr, bool computeTightBounds = false);

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
   * @param shapeHitTest Whether to check the actual shape of the layer (true) or just the bounding
   * box (false). Note that Image layers are always checked against their bounding box. You can draw
   * image layers to a Surface and use the Surface::getColor() method to check the actual pixels.
   * @return true if the layer overlaps or intersects with the specified point, false otherwise.
   */
  bool hitTestPoint(float x, float y, bool shapeHitTest = false);

  /**
   * Draws the layer and all its children onto the given canvas. You can specify the alpha and blend
   * mode to control how the layer is drawn. Note: The layer is drawn in its local space without
   * applying its own matrix, alpha, blend mode, visible, scrollRect, or mask.
   * Note: Using a Canvas without a Surface may cause incorrect blending when passThroughBackground
   * is enabled.
   * @param canvas The canvas to draw the layer on.
   * @param alpha The alpha transparency value used for drawing the layer and its children.
   * @param blendMode The blend mode used to composite the layer with the existing content on the
   * canvas.
   */
  void draw(Canvas* canvas, float alpha = 1.0f, BlendMode blendMode = BlendMode::SrcOver);

 protected:
  Layer();

  /**
   * Marks the layer's contents as changed and in need of an update. This will trigger a call to
   * onUpdateContent() to update the layer's contents.
   */
  void invalidateContent();

  /**
   * Called when the layer’s contents needs to be updated. Subclasses should override this method to
   * update the layer’s contents, typically by drawing on a canvas obtained from the given
   * LayerRecorder.
   * @param recorder The LayerRecorder used to record the layer's contents.
   */
  virtual void onUpdateContent(LayerRecorder* recorder);

  /**
   * Attaches a property to this layer.
   */
  void attachProperty(LayerProperty* property);

  /**
   * Detaches a property from this layer.
   */
  void detachProperty(LayerProperty* property);

 private:
  /**
   * Marks the layer as needing to be redrawn. Unlike invalidateContent(), this method only marks
   * the layer as dirty and does not update the layer content.
   */
  void invalidateTransform();

  /**
   * Marks the layer's descendents as changed and needing to be redrawn.
   */
  void invalidateDescendents();

  void invalidate();

  Rect getBoundsInternal(const Matrix3D& coordinateMatrix, bool computeTightBounds);

  Rect computeBounds(const Matrix3D& coordinateMatrix, bool computeTightBounds);

  void onAttachToRoot(RootLayer* rootLayer);

  void onDetachFromRoot();

  int doGetChildIndex(const Layer* child) const;

  bool doContains(const Layer* child) const;

  Matrix3D getGlobalMatrix() const;

  Matrix3D getMatrixWithScrollRect() const;

  LayerContent* getContent();

  std::shared_ptr<ImageFilter> getImageFilter(float contentScale);

  virtual void drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                         const Matrix3D* transform3D = nullptr);

  void drawOffscreen(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                     const Matrix3D* transform3D, const std::shared_ptr<MaskFilter>& maskFilter);

  void drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha);

  void drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha,
                    const std::vector<LayerStyleExtraSourceType>& styleExtraSourceTypes);

  void drawContents(const DrawArgs& args, Canvas* canvas, float alpha,
                    const LayerStyleSource* layerStyleSource = nullptr,
                    const Layer* stopChild = nullptr,
                    const std::vector<LayerStyleExtraSourceType>& styleExtraSourceTypes = {});

  bool drawChildren(const DrawArgs& args, Canvas* canvas, float alpha,
                    const Layer* stopChild = nullptr);

  float drawBackgroundLayers(const DrawArgs& args, Canvas* canvas);

  std::unique_ptr<LayerStyleSource> getLayerStyleSource(const DrawArgs& args, const Matrix& matrix,
                                                        bool excludeContour = false);

  std::shared_ptr<Image> getBackgroundImage(const DrawArgs& args, float contentScale,
                                            Point* offset);

  /**
   * Gets the background image of the minimum axis-aligned bounding box after drawing the layer
   * subtree with the current layer as the root node.
   */
  std::shared_ptr<Image> getBoundsBackgroundImage(const DrawArgs& args, float contentScale,
                                                  Point* offset);

  void drawBackgroundImage(const DrawArgs& args, Canvas& canvas);

  void drawLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                       const LayerStyleSource* source, LayerStylePosition position);

  void drawLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                       const LayerStyleSource* source, LayerStylePosition position,
                       const std::vector<LayerStyleExtraSourceType>& styleExtraSourceTypes);

  void drawBackgroundLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                                 const Matrix3D& transform);

  bool getLayersUnderPointInternal(float x, float y, std::vector<std::shared_ptr<Layer>>* results);

  bool prepareMask(const DrawArgs& args, Canvas* canvas, std::shared_ptr<MaskFilter>* maskFilter);

  MaskData getMaskData(const DrawArgs& args, float scale,
                       const std::optional<Rect>& layerClipBounds);

  Matrix3D getRelativeMatrix(const Layer* targetCoordinateSpace) const;

  bool hasValidMask() const;

  void updateRenderBounds(std::shared_ptr<RegionTransformer> transformer = nullptr,
                          bool forceDirty = false);

  void checkBackgroundStyles(std::shared_ptr<RegionTransformer> transformer);

  void updateBackgroundBounds(float contentScale);

  void propagateLayerState();

  bool hasBackgroundStyle();

  std::shared_ptr<BackgroundContext> createBackgroundContext(
      Context* context, const Rect& drawRect, const Matrix& viewMatrix, bool fullLayer = false,
      std::shared_ptr<ColorSpace> colorSpace = nullptr) const;

  static std::shared_ptr<Picture> RecordPicture(DrawMode mode, float contentScale,
                                                const std::function<void(Canvas*)>& drawFunction);

  bool shouldPassThroughBackground(BlendMode blendMode, const Matrix3D* transform3D) const;

  bool canUseSubtreeCache(const DrawArgs& args, BlendMode blendMode, const Matrix3D* transform3D);

  SubtreeCache* getValidSubtreeCache(const DrawArgs& args, int longEdge, const Rect& layerBounds);

  std::shared_ptr<Image> createSubtreeCacheImage(const DrawArgs& args, float contentScale,
                                                 const Rect& scaledBounds, Matrix* drawingMatrix);

  bool drawWithSubtreeCache(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                            const Matrix3D* transform3D,
                            const std::shared_ptr<MaskFilter>& maskFilter);

  std::shared_ptr<Image> getContentImage(
      const DrawArgs& args, const Matrix& contentMatrix, const std::optional<Rect>& clipBounds,
      const std::vector<LayerStyleExtraSourceType>& extraSourceTypes, Matrix* imageMatrix);

  std::shared_ptr<Image> getPassThroughContentImage(
      const DrawArgs& args, Canvas* canvas, const std::optional<Rect>& clipBounds,
      const std::vector<LayerStyleExtraSourceType>& extraSourceTypes, Matrix* imageMatrix);

  std::optional<Rect> computeContentBounds(const std::optional<Rect>& clipBounds,
                                           bool excludeEffects);

  /**
   * Returns the equivalent transformation matrix adapted for a custom anchor point.
   * The matrix is defined based on a local coordinate system, with the transformation anchor point
   * being the origin of that coordinate system. This function returns an affine transformation
   * matrix that produces the same visual effect when using any point within this coordinate system
   * as the new origin and anchor point.
   * @param matrix The original transformation matrix.
   * @param anchor The specified anchor point.
   */
  Matrix3D anchorAdaptedMatrix(const Matrix3D& matrix, const Point& anchor) const;

  void invalidateSubtree();

  void updateStaticSubtreeFlags();

  struct {
    bool dirtyContent : 1;        // layer's content needs updating
    bool dirtyContentBounds : 1;  // layer's content bounds needs updating
    bool dirtyDescendents : 1;    // a descendant layer needs redrawing
    bool dirtyTransform : 1;      // the layer and its children need redrawing
    bool visible : 1;
    bool allowsEdgeAntialiasing : 1;
    bool allowsGroupOpacity : 1;
    bool excludeChildEffectsInLayerStyle : 1;
    bool passThroughBackground : 1;
    bool hasBlendMode : 1;
    bool matrix3DIsAffine : 1;  // Whether the matrix3D is equivalent to a 2D affine matrix
    bool staticSubtree : 1;  // Whether the subtree (content, children, filters, styles) is static.
    uint8_t blendMode : 5;
    uint8_t maskType : 2;
  } bitFields = {};
  std::string _name;
  float _alpha = 1.0f;
  // The actual transformation matrix that determines the geometric position of the layer
  Matrix3D _matrix3D = {};
  std::shared_ptr<Layer> _mask = nullptr;
  Layer* maskOwner = nullptr;
  std::unique_ptr<Rect> _scrollRect = nullptr;
  RootLayer* _root = nullptr;
  Layer* _parent = nullptr;
  std::vector<std::shared_ptr<Layer>> _children = {};
  std::vector<std::shared_ptr<LayerFilter>> _filters = {};
  std::vector<std::shared_ptr<LayerStyle>> _layerStyles = {};
  std::unique_ptr<SubtreeCache> subtreeCache;
  std::shared_ptr<LayerContent> layerContent = nullptr;
  Rect renderBounds = {};                       // in global coordinates
  Rect* contentBounds = nullptr;                //  in global coordinates
  std::unique_ptr<Rect> localBounds = nullptr;  // in local coordinates

  // if > 0, means the layer or any of its descendants has a background style
  float maxBackgroundOutset = 0.f;
  float minBackgroundOutset = std::numeric_limits<float>::max();

  friend class RootLayer;
  friend class DisplayList;
  friend class LayerProperty;
  friend class LayerSerialization;
};
}  // namespace tgfx
