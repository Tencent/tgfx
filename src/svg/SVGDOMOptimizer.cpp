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

#include "SVGDOMOptimizer.h"
#include <string>
#include <vector>
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGFeBlend.h"
#include "tgfx/svg/node/SVGFeComposite.h"
#include "tgfx/svg/node/SVGFilter.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGPath.h"
#include "tgfx/svg/node/SVGRect.h"

namespace tgfx {

static std::string GetFilterUrl(const SVGNode* node) {
  const auto& filterProp = node->getFilter();
  if (!filterProp.isValue()) {
    return "";
  }
  auto filterIRI = filterProp.get();
  if (!filterIRI.has_value() || filterIRI->type() != SVGFuncIRI::Type::IRI) {
    return "";
  }
  return filterIRI->iri().iri();
}

void SVGDOMOptimizer::OptimizeFilterPairs(SVGContainer* root, const SVGIDMapper& idMapper) {
  if (!root) {
    return;
  }
  processContainer(root, idMapper);
}

void SVGDOMOptimizer::processContainer(SVGContainer* container, const SVGIDMapper& idMapper) {
  // Collect child containers first, then recurse. This avoids iterating
  // container->getChildren() while a recursive call might modify a child's vector.
  std::vector<SVGContainer*> childContainers;
  for (const auto& child : container->getChildren()) {
    if (child->hasChildren()) {
      childContainers.push_back(static_cast<SVGContainer*>(child.get()));
    }
  }
  for (auto* childContainer : childContainers) {
    processContainer(childContainer, idMapper);
  }

  // Now scan for filter-pair patterns in this container's children.
  auto& children = container->children;
  if (children.size() < 2) {
    return;
  }

  // Scan from the end to avoid index invalidation during erasure.
  size_t idx = children.size() - 1;
  while (idx > 0) {
    size_t i = idx - 1;
    auto* contentNode = children[i].get();

    // Content node must be a shape element without a filter.
    if (!isShapeElement(contentNode) || !GetFilterUrl(contentNode).empty()) {
      --idx;
      continue;
    }

    // Look ahead for the filter group. It may be at i+1 or i+2 (if defs is in between).
    size_t filterGroupIdx = 0;
    bool found = false;
    for (size_t j = i + 1; j < children.size() && j <= i + 2; ++j) {
      if (children[j]->tag() == SVGTag::Defs) {
        continue;
      }
      if (isPureFilterGroup(children[j].get())) {
        filterGroupIdx = j;
        found = true;
      }
      break;
    }
    if (!found) {
      --idx;
      continue;
    }

    // Unwrap nested filter groups to find the leaf content node.
    auto chain = unwrapFilterGroups(children[filterGroupIdx].get());
    if (chain.filterUrls.empty() || !chain.leafNode) {
      --idx;
      continue;
    }

    // Leaf must be a matching shape with white fill.
    if (!isShapeElement(chain.leafNode) || !isWhiteFill(chain.leafNode) ||
        !isMatchingShape(contentNode, chain.leafNode)) {
      --idx;
      continue;
    }

    // Verify all filters in the chain are InnerShadowOnly. Skip if any filter is not
    // a recognized InnerShadowOnly pattern to avoid mismerging unrelated SVG structures.
    bool allInnerShadowOnly = true;
    for (const auto& filterUrl : chain.filterUrls) {
      auto it = idMapper.find(filterUrl);
      if (it == idMapper.end() || it->second->tag() != SVGTag::Filter) {
        allInnerShadowOnly = false;
        break;
      }
      auto* filterNode = static_cast<const SVGContainer*>(it->second.get());
      if (!isInnerShadowOnlyFilter(filterNode)) {
        allInnerShadowOnly = false;
        break;
      }
    }
    if (!allInnerShadowOnly) {
      --idx;
      continue;
    }

    // Upgrade InnerShadowOnly filters to InnerShadow so the filter preserves the original
    // content when applied directly to the content element.
    for (const auto& filterUrl : chain.filterUrls) {
      auto it = idMapper.find(filterUrl);
      auto* filterNode = static_cast<SVGContainer*>(it->second.get());
      upgradeToInnerShadow(filterNode);
    }

    // Apply the innermost filter to the content node.
    auto& innermostFilterUrl = chain.filterUrls.front();
    SVGFuncIRI filterIRI(SVGIRI(SVGIRI::Type::Local, innermostFilterUrl));
    SVGProperty<SVGFuncIRI, false> filterProp(filterIRI);
    children[i]->setFilter(filterProp);

    if (chain.filterUrls.size() == 1) {
      // Simple case: remove the filter group entirely.
      children.erase(children.begin() + static_cast<ptrdiff_t>(filterGroupIdx));
    } else {
      // Multi-filter case: replace the leaf node inside the innermost group with the content node,
      // and remove the original content node position.
      // The outermost <g> stays but now wraps the content node (with innermost filter).

      // Find the innermost group that directly contains the leaf.
      SVGContainer* innermostGroup = nullptr;
      SVGNode* current = children[filterGroupIdx].get();
      while (current->hasChildren()) {
        auto* cont = static_cast<SVGContainer*>(current);
        auto& ch = cont->children;
        if (ch.size() == 1 && isPureFilterGroup(ch[0].get())) {
          current = ch[0].get();
        } else if (ch.size() == 1 && isShapeElement(ch[0].get())) {
          innermostGroup = cont;
          break;
        } else {
          break;
        }
      }

      if (innermostGroup) {
        // Replace leaf with the content node (which now has innermost filter set).
        auto contentShared = children[i];
        innermostGroup->children[0] = contentShared;
        children.erase(children.begin() + static_cast<ptrdiff_t>(i));
      } else {
        // Fallback: just remove the filter group (shouldn't happen with well-formed tgfx output).
        children.erase(children.begin() + static_cast<ptrdiff_t>(filterGroupIdx));
      }
    }
    // After erase, idx now points beyond the new size or at a valid position.
    // Reset to re-check from the current end.
    idx = (i > 0) ? i : 0;
    if (idx == 0) {
      break;
    }
  }
}

bool SVGDOMOptimizer::isPureFilterGroup(const SVGNode* node) {
  if (node->tag() != SVGTag::G) {
    return false;
  }
  if (GetFilterUrl(node).empty()) {
    return false;
  }
  auto* transformable = static_cast<const SVGTransformableNode*>(node);
  if (!transformable->getTransform().isIdentity()) {
    return false;
  }
  if (node->getOpacity().isValue()) {
    return false;
  }
  if (node->getClipPath().isValue()) {
    return false;
  }
  if (node->getMask().isValue()) {
    return false;
  }
  return true;
}

bool SVGDOMOptimizer::isShapeElement(const SVGNode* node) {
  auto t = node->tag();
  return t == SVGTag::Path || t == SVGTag::Rect || t == SVGTag::Circle || t == SVGTag::Ellipse ||
         t == SVGTag::Line || t == SVGTag::Polygon || t == SVGTag::Polyline;
}

bool SVGDOMOptimizer::isMatchingShape(const SVGNode* a, const SVGNode* b) {
  if (a->tag() != b->tag()) {
    return false;
  }
  auto* transformableA = static_cast<const SVGTransformableNode*>(a);
  auto* transformableB = static_cast<const SVGTransformableNode*>(b);
  if (transformableA->getTransform() != transformableB->getTransform()) {
    return false;
  }

  switch (a->tag()) {
    case SVGTag::Path: {
      auto* pathA = static_cast<const SVGPath*>(a);
      auto* pathB = static_cast<const SVGPath*>(b);
      return pathA->getShapePath() == pathB->getShapePath();
    }
    case SVGTag::Rect: {
      auto* rectA = static_cast<const SVGRect*>(a);
      auto* rectB = static_cast<const SVGRect*>(b);
      return rectA->getX() == rectB->getX() && rectA->getY() == rectB->getY() &&
             rectA->getWidth() == rectB->getWidth() && rectA->getHeight() == rectB->getHeight() &&
             rectA->getRx() == rectB->getRx() && rectA->getRy() == rectB->getRy();
    }
    default:
      return false;
  }
}

bool SVGDOMOptimizer::isWhiteFill(const SVGNode* node) {
  const auto& fillProp = node->getFill();
  if (!fillProp.isValue()) {
    return false;
  }
  auto fill = fillProp.get();
  if (!fill.has_value()) {
    return false;
  }
  if (fill->type() != SVGPaint::Type::Color) {
    return false;
  }
  auto color = fill->color().color();
  return color == Color::White();
}

SVGDOMOptimizer::FilterChain SVGDOMOptimizer::unwrapFilterGroups(SVGNode* node) {
  FilterChain chain;
  SVGNode* current = node;

  while (current) {
    if (!isPureFilterGroup(current)) {
      // This is the leaf node.
      chain.leafNode = current;
      break;
    }
    chain.filterUrls.push_back(GetFilterUrl(current));
    auto* container = static_cast<SVGContainer*>(current);
    auto& ch = container->children;
    if (ch.size() != 1) {
      chain.leafNode = nullptr;
      break;
    }
    current = ch[0].get();
  }

  return chain;
}

bool SVGDOMOptimizer::isInnerShadowOnlyFilter(const SVGContainer* filterNode) {
  // InnerShadowOnly pattern: ends with feColorMatrix after feComposite(arithmetic),
  // without a trailing feBlend.
  const auto& ch = filterNode->getChildren();
  if (ch.size() < 4) {
    return false;
  }
  for (size_t i = 0; i < ch.size(); i++) {
    if (ch[i]->tag() == SVGTag::FeComposite) {
      auto* composite = static_cast<const SVGFeComposite*>(ch[i].get());
      if (composite->getOperator() == SVGFeCompositeOperator::Arithmetic) {
        // Check: next is feColorMatrix and there's no feBlend after it.
        if (i + 1 < ch.size() && ch[i + 1]->tag() == SVGTag::FeColorMatrix) {
          if (i + 2 >= ch.size() || ch[i + 2]->tag() != SVGTag::FeBlend) {
            return true;
          }
        }
        break;
      }
    }
  }
  return false;
}

void SVGDOMOptimizer::upgradeToInnerShadow(SVGContainer* filterNode) {
  // Append a feBlend node to convert InnerShadowOnly → InnerShadow.
  // The feBlend merges the shadow result with the original SourceGraphic.
  auto feBlend = SVGFeBlend::Make();
  feBlend->setIn2(SVGFeInputType(SVGFeInputType::Type::SourceGraphic));
  filterNode->appendChild(std::move(feBlend));
}

}  // namespace tgfx
