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

#pragma once

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/RRect.h"

namespace tgfx {
class PathRef;

/**
 * Path contains geometry. Path may be empty or contain one or more verbs that outline a figure.
 * Path always starts with a move verb to a Cartesian coordinate, and may be followed by additional
 * verbs that add lines or curves. Adding a close verb makes the geometry into a continuous loop, a
 * closed contour. Path may contain any number of contours, each beginning with a move verb.
 */
class Path {
 public:
  /**
   * Creates an empty path.
   */
  Path();

  /**
   * Compares a and b; returns true if PathFillType, verb array, and Point array are equivalent.
   */
  friend bool operator==(const Path& a, const Path& b);

  /**
   * Compares a and b; returns true if PathFillType, verb array, and Point array are not equivalent.
   */
  friend bool operator!=(const Path& a, const Path& b);

  /**
   * Returns true if this Path is the same as the specified Path. This method is faster than
   * operator== but may return false for paths that are equivalent but not identical, as it only
   * compares the internal reference without a deep comparison.
   */
  bool isSame(const Path& other) const {
    return pathRef == other.pathRef;
  }

  /**
   * Returns PathFillType, the rule used to fill Path. PathFillType of a new Path is
   * PathFillType::Winding.
   */
  PathFillType getFillType() const;

  /**
   * Sets PathFillType, the rule used to fill Path.
   */
  void setFillType(PathFillType fillType);

  /**
   * Returns if PathFillType is InverseWinding or InverseEvenOdd.
   */
  bool isInverseFillType() const;

  /**
   * Replaces PathFillType with its inverse.
   */
  void toggleInverseFillType();

  /**
   * Returns true if Path contains only one line;
   */
  bool isLine(Point line[2] = nullptr) const;

  /**
   * Returns true if Path is equivalent to Rect when filled, Otherwise returns false, and leaves
   * rect unchanged. The rect may be smaller than the Path bounds. Path bounds may include
   * PathVerb::Move points that do not alter the area drawn by the returned rect.
   * @param rect storage for bounds of Rect; may be nullptr.
   */
  bool isRect(Rect* rect = nullptr, bool* closed = nullptr, bool* reversed = nullptr) const;

  /**
   * Returns true if this path is equivalent to an oval or circle when filled. Otherwise, returns
   * false and leaves the bounds unchanged.
   * @param bounds  storage for bounding Rect of oval; may be nullptr.
   */
  bool isOval(Rect* bounds = nullptr) const;

  /**
   * Returns true if Path is equivalent to RRect when filled, Otherwise returns false, and leaves
   * rRect unchanged. Please note that this method returns false if the path is representable as
   * oval, circle, or Rect.
   * @param rRect  storage for RRect; may be nullptr.
   */
  bool isRRect(RRect* rRect) const;

  /**
   * Returns the bounds of the path's points. If the path contains 0 or 1 points, the bounds is set
   * to (0,0,0,0), and isEmpty() will return true. Note: this bounds may be larger than the actual
   * shape, since curves do not extend as far as their control points.
   */
  Rect getBounds() const;

  /**
   * Returns true if Path is empty.
   */
  bool isEmpty() const;

  /**
   * Returns true if the point (x, y) is contained by Path, taking into account PathFillType.
   */
  bool contains(float x, float y) const;

  /**
   * Returns true if rect is contained by Path. This method is conservative. it may return false
   * when rect is actually contained by Path. For now, only returns true if Path has one contour.
   */
  bool contains(const Rect& rect) const;

  /**
   * Adds beginning of contour at Point (x, y).
   */
  void moveTo(float x, float y);

  /**
   * Adds beginning of contour at point.
   */
  void moveTo(const Point& point);

  /**
   * Adds a line from last point to (x, y).
   */
  void lineTo(float x, float y);

  /**
   * Adds a line from last point to new point.
   */
  void lineTo(const Point& point);

  /**
   * Adds a quad curve from last point towards (controlX, controlY), ending at (x, y).
   */
  void quadTo(float controlX, float controlY, float x, float y);

  /**
   * Adds a quad curve from last point towards control, ending at point.
   */
  void quadTo(const Point& control, const Point& point);

  /**
   * Adds a cubic curve from last point towards (controlX1, controlY1), then towards
   * (controlX2, controlY2), ending at (x, y).
   */
  void cubicTo(float controlX1, float controlY1, float controlX2, float controlY2, float x,
               float y);

  /**
   * Adds a cubic curve from last point towards control1, then towards control2, ending at (x, y).
   */
  void cubicTo(const Point& control1, const Point& control2, const Point& point);

  /**
   * Adds a conic curve from last point towards (controlX, controlY), ending at (x, y), weighted by
   * weight. Conics can represent circular, elliptical, parabolic, or hyperbolic arcs depending on
   * the weight value:
   * - weight < 1: elliptical arc
   * - weight == 1: equivalent to quadTo (parabolic arc)
   * - weight > 1: hyperbolic arc
   * - weight == sqrt(2)/2 ≈ 0.707: exact 90-degree circular arc
   * @param controlX  x-coordinate of the control point
   * @param controlY  y-coordinate of the control point
   * @param x         x-coordinate of the end point
   * @param y         y-coordinate of the end point
   * @param weight    conic weight determining the curve type
   */
  void conicTo(float controlX, float controlY, float x, float y, float weight);

  /**
   * Adds a conic curve from last point towards control, ending at point, weighted by weight. Conics
   * can represent circular, elliptical, parabolic, or hyperbolic arcs depending on the weight
   * value:
   * - weight < 1: elliptical arc
   * - weight == 1: equivalent to quadTo (parabolic arc)
   * - weight > 1: hyperbolic arc
   * - weight == sqrt(2)/2 ≈ 0.707: exact 90-degree circular arc
   * @param control  the control point
   * @param point    the end point
   * @param weight   conic weight determining the curve type
   */
  void conicTo(const Point& control, const Point& point, float weight);

  /**
   * Append a line and arc to the current path. This is the same as the PostScript call "arct".
   */
  void arcTo(float x1, float y1, float x2, float y2, float radius);

  /**
   * Append a line and arc to the current path. This is the same as the PostScript call "arct".
   */
  void arcTo(const Point& p1, const Point& p2, float radius);

  /**
   * Appends an arc to the Path. The arc is represented by one or more conic sections that describe
   * part of an oval with radii (rx, ry) rotated by xAxisRotate degrees. The arc curves from the 
   * last point in the Path to (x, y), choosing one of four possible routes: clockwise or 
   * counterclockwise, and smaller or larger.
   * The arc sweep is always less than 360 degrees. If either radius is zero, or if the last point 
   * in the Path equals (x, y), a line to (x, y) is appended instead. If both radii are greater 
   * than zero but too small to fit the arc, they are scaled to fit.
   * This method appends up to four conic curves to represent the arc.
   * It implements the functionality of the SVG arc, although the SVG sweep-flag value is the 
   * opposite of the integer value of the sweep parameter; SVG uses 1 for clockwise, while 
   * counterclockwise is represented by zero.
   * 
   * @param rx            x radius before x-axis rotation
   * @param ry            y radius before x-axis rotation
   * @param xAxisRotate   x-axis rotation in degrees; positive values are clockwise
   * @param largeArc      chooses the larger or smaller arc
   * @param reversed      chooses the rotation direction; false for clockwise
   * @param endPoint      end point of the arc
   */
  void arcTo(float rx, float ry, float xAxisRotate, PathArcSize largeArc, bool reversed,
             Point endPoint);
  /**
   * Closes the current contour of Path. A closed contour connects the first and last Point with
   * line, forming a continuous loop.
   */
  void close();

  /**
   * Adds a rect to Path. If reversed is false, Rect begins at startIndex point and continues
   * clockwise if reversed is false, counterclockwise if reversed is true. The indices of all points
   * are as follows:
   *  0         1
   *   *-------*
   *   |       |
   *   *-------*
   *  3         2
   */
  void addRect(const Rect& rect, bool reversed = false, unsigned startIndex = 0);

  /**
   * Adds a rect to Path. If reversed is false, Rect begins at startIndex point and continues
   * clockwise if reversed is false, counterclockwise if reversed is true. The indices of all points
   * are as follows:
   *  0         1
   *   *-------*
   *   |       |
   *   *-------*
   *  3         2
   */
  void addRect(float left, float top, float right, float bottom, bool reversed = false,
               unsigned startIndex = 0);

  /**
   * Adds a oval to path. Oval is upright ellipse bounded by Rect oval with radius equal to half
   * oval width and half oval height. Oval begins at startIndex point and continues clockwise if
   * reversed is false, counterclockwise if reversed is true. The indices of all points are as
   * follows:
   *         0
   *       --*--
   *     |       |
   *   3 *       * 1
   *     |       |
   *       --*--
   *         2
   */
  void addOval(const Rect& oval, bool reversed = false, unsigned startIndex = 0);

  /**
   * Appends arc to Path, as the start of new contour. Arc added is part of ellipse bounded by oval,
   * from startAngle through sweepAngle. Both startAngle and sweepAngle are measured in degrees,
   * where zero degrees is aligned with the positive x-axis, and positive sweeps extends arc
   * clockwise. If sweepAngle <= -360, or sweepAngle >= 360; and startAngle modulo 90 is nearly
   * zero, append oval instead of arc. Otherwise, sweepAngle values are treated modulo 360, and arc
   * may or may not draw depending on numeric rounding.
   *
   * @param oval        bounds of ellipse containing arc
   * @param startAngle  starting angle of arc in degrees
   * @param sweepAngle  sweep, in degrees. Positive is clockwise; treated modulo 360
   */
  void addArc(const Rect& oval, float startAngle, float sweepAngle);

  /**
   * Adds a round rect to path. creating a new closed contour, each corner is 90 degrees of an
   * ellipse with radius (radiusX, radiusY). The round rect begins at startIndex point and continues
   * clockwise if reversed is false, counterclockwise if reversed is true. The indices of all points
   * are as follows:
   *      0      1
   *      *------*
   *   7 *        * 2
   *     |        |
   *   6 *        * 3
   *      *------*
   *      5      4
   */
  void addRoundRect(const Rect& rect, float radiusX, float radiusY, bool reversed = false,
                    unsigned startIndex = 0);

  /**
   * Adds a round rect to path. creating a new closed contour, each corner is 90 degrees of an
   * ellipse with radii from the array. The round rect begins at startIndex point and continues
   * clockwise if reversed is false, counterclockwise if reversed is true. The indices of all points
   * are as follows:
   *      0      1
   *      *------*
   *   7 *        * 2
   *     |        |
   *   6 *        * 3
   *      *------*
   *      5      4
   * Radii order: top-left, top-right, bottom-right, bottom-left
   */
  void addRoundRect(const Rect& rect, const std::array<Point, 4>& radii, bool reversed = false,
                    unsigned startIndex = 0);

  /**
   * Adds a RRect to the path, creating a new closed contour. The round rect begins at startIndex
   * point and continues clockwise if reversed is false, counterclockwise if reversed is true.
   * The indices of all points are as follows:
   *      0      1
   *      *------*
   *   7 *        * 2
   *     |        |
   *   6 *        * 3
   *      *------*
   *      5      4
   */
  void addRRect(const RRect& rRect, bool reversed = false, unsigned startIndex = 0);

  /**
   * Adds a src to this Path.
   */
  void addPath(const Path& src, PathOp op = PathOp::Append);

  /**
   * Sets Path to its initial state. Internal storage associated with Path is released.
   */
  void reset();

  /**
   * Applies matrix to this Path, this transforms verb array, Point array, and weight.
   */
  void transform(const Matrix& matrix);

  /**
   * Applies a 3D matrix to this Path, transforming the verb array, point array, and weights.
   */
  void transform3D(const Matrix3D& matrix);

  /**
   * Reveres this path from back to front.
   */
  void reverse();

  /**
   * Iterates through verb array and associated Point array. The iterator callback receives the verb
   * type, points array, and conic weight (only valid for PathVerb::Conic, otherwise 0).
   */
  void decompose(const PathIterator& iterator, void* info = nullptr) const;

  /**
   * Returns the number of points in Path.
   */
  int countPoints() const;

  /**
   * Returns the number of verbs in Path.
   */
  int countVerbs() const;

  /**
   * Returns last point on Path in lastPoint. Returns false if point array is empty.
   */
  bool getLastPoint(Point* lastPoint) const;

 private:
  std::shared_ptr<PathRef> pathRef = nullptr;

  PathRef* writableRef();

  friend class PathRef;
};
}  // namespace tgfx
