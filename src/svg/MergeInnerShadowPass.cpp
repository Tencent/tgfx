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

#include "MergeInnerShadowPass.h"
#include <string>
#include <vector>
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGCircle.h"
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGEllipse.h"
#include "tgfx/svg/node/SVGFeBlend.h"
#include "tgfx/svg/node/SVGFeComposite.h"
#include "tgfx/svg/node/SVGFilter.h"
#include "tgfx/svg/node/SVGLine.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGPath.h"
#include "tgfx/svg/node/SVGPoly.h"
#include "tgfx/svg/node/SVGRect.h"

namespace tgfx {

namespace {

struct FilterChain {
  std::vector<std::string> filterUrls;
  SVGNode* leafNode = nullptr;
};

struct MergeCandidate {
  size_t contentIndex;
  size_t filterGroupIndex;
  FilterChain chain;
};

std::string GetFilterUrl(const SVGNode* node) {
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

bool IsPureFilterGroup(const SVGNode* node) {
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

bool IsMatchingShape(const SVGNode* a, const SVGNode* b) {
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
    case SVGTag::Circle: {
      auto* circleA = static_cast<const SVGCircle*>(a);
      auto* circleB = static_cast<const SVGCircle*>(b);
      return circleA->getCx() == circleB->getCx() && circleA->getCy() == circleB->getCy() &&
             circleA->getR() == circleB->getR();
    }
    case SVGTag::Ellipse: {
      auto* ellipseA = static_cast<const SVGEllipse*>(a);
      auto* ellipseB = static_cast<const SVGEllipse*>(b);
      return ellipseA->getCx() == ellipseB->getCx() && ellipseA->getCy() == ellipseB->getCy() &&
             ellipseA->getRx() == ellipseB->getRx() && ellipseA->getRy() == ellipseB->getRy();
    }
    case SVGTag::Line: {
      auto* lineA = static_cast<const SVGLine*>(a);
      auto* lineB = static_cast<const SVGLine*>(b);
      return lineA->getX1() == lineB->getX1() && lineA->getY1() == lineB->getY1() &&
             lineA->getX2() == lineB->getX2() && lineA->getY2() == lineB->getY2();
    }
    case SVGTag::Polygon:
    case SVGTag::Polyline: {
      auto* polyA = static_cast<const SVGPoly*>(a);
      auto* polyB = static_cast<const SVGPoly*>(b);
      return polyA->getPoints() == polyB->getPoints();
    }
    default:
      return false;
  }
}

bool IsWhiteFill(const SVGNode* node) {
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

FilterChain UnwrapFilterGroups(SVGNode* node) {
  FilterChain chain;
  SVGNode* current = node;

  while (current) {
    if (!IsPureFilterGroup(current)) {
      chain.leafNode = current;
      break;
    }
    chain.filterUrls.push_back(GetFilterUrl(current));
    auto* container = static_cast<SVGContainer*>(current);
    auto& ch = container->getChildren();
    if (ch.size() != 1) {
      chain.leafNode = nullptr;
      break;
    }
    current = ch[0].get();
  }

  return chain;
}

bool IsInnerShadowOnlyFilter(const SVGContainer* filterNode) {
  const auto& ch = filterNode->getChildren();
  if (ch.size() < 4) {
    return false;
  }
  for (size_t i = 0; i < ch.size(); i++) {
    if (ch[i]->tag() == SVGTag::FeComposite) {
      auto* composite = static_cast<const SVGFeComposite*>(ch[i].get());
      if (composite->getOperator() == SVGFeCompositeOperator::Arithmetic) {
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

void UpgradeToInnerShadow(SVGContainer* filterNode) {
  auto feBlend = SVGFeBlend::Make();
  feBlend->setIn2(SVGFeInputType(SVGFeInputType::Type::SourceGraphic));
  filterNode->appendChild(std::move(feBlend));
}

std::vector<MergeCandidate> CollectMergeCandidates(SVGContainer* container,
                                                   const SVGIDMapper& idMapper) {
  std::vector<MergeCandidate> candidates;
  auto& children = container->getChildren();
  if (children.size() < 2) {
    return candidates;
  }

  for (size_t i = 0; i + 1 < children.size(); ++i) {
    auto* contentNode = children[i].get();

    if (!IsMatchingShape(contentNode, contentNode) || !GetFilterUrl(contentNode).empty()) {
      continue;
    }

    // Look ahead for the filter group at i+1 or i+2 (if defs is in between).
    size_t filterGroupIdx = 0;
    bool found = false;
    for (size_t j = i + 1; j < children.size() && j <= i + 2; ++j) {
      if (children[j]->tag() == SVGTag::Defs) {
        continue;
      }
      if (IsPureFilterGroup(children[j].get())) {
        filterGroupIdx = j;
        found = true;
      }
      break;
    }
    if (!found) {
      continue;
    }

    auto chain = UnwrapFilterGroups(children[filterGroupIdx].get());
    if (chain.filterUrls.empty() || !chain.leafNode) {
      continue;
    }

    if (!IsMatchingShape(chain.leafNode, chain.leafNode) || !IsWhiteFill(chain.leafNode) ||
        !IsMatchingShape(contentNode, chain.leafNode)) {
      continue;
    }

    // Verify all filters in the chain are InnerShadowOnly.
    bool allInnerShadowOnly = true;
    for (const auto& filterUrl : chain.filterUrls) {
      auto it = idMapper.find(filterUrl);
      if (it == idMapper.end() || it->second->tag() != SVGTag::Filter) {
        allInnerShadowOnly = false;
        break;
      }
      auto* filterNode = static_cast<const SVGContainer*>(it->second.get());
      if (!IsInnerShadowOnlyFilter(filterNode)) {
        allInnerShadowOnly = false;
        break;
      }
    }
    if (!allInnerShadowOnly) {
      continue;
    }

    candidates.push_back({i, filterGroupIdx, std::move(chain)});
  }

  return candidates;
}

void ExecuteMerge(SVGContainer* container, const std::vector<MergeCandidate>& candidates,
                  const SVGIDMapper& idMapper) {
  // Process from back to front to keep earlier indices valid after erasure.
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    const auto& candidate = *it;
    auto& children = container->getChildren();
    size_t i = candidate.contentIndex;
    size_t filterGroupIdx = candidate.filterGroupIndex;
    const auto& chain = candidate.chain;

    // Check filter reference safety: skip if any filter is shared by multiple elements.
    bool filterShared = false;
    for (const auto& filterUrl : chain.filterUrls) {
      auto filterIt = idMapper.find(filterUrl);
      // use_count > 2 means: one in idMapper + one in DOM tree = 2 is normal;
      // anything more means another element also references this filter.
      if (filterIt != idMapper.end() && filterIt->second.use_count() > 2) {
        filterShared = true;
        break;
      }
    }
    if (filterShared) {
      continue;
    }

    // Upgrade InnerShadowOnly filters to InnerShadow.
    for (const auto& filterUrl : chain.filterUrls) {
      auto filterIt = idMapper.find(filterUrl);
      auto* filterNode = static_cast<SVGContainer*>(filterIt->second.get());
      UpgradeToInnerShadow(filterNode);
    }

    // Apply the innermost filter to the content node.
    auto& innermostFilterUrl = chain.filterUrls.front();
    SVGFuncIRI filterIRI(SVGIRI(SVGIRI::Type::Local, innermostFilterUrl));
    SVGProperty<SVGFuncIRI, false> filterProp(filterIRI);
    children[i]->setFilter(filterProp);

    if (chain.filterUrls.size() == 1) {
      container->eraseChild(filterGroupIdx);
    } else {
      // Multi-filter case: replace the leaf node inside the innermost group with the content node.
      SVGContainer* innermostGroup = nullptr;
      SVGNode* current = children[filterGroupIdx].get();
      while (current->hasChildren()) {
        auto* cont = static_cast<SVGContainer*>(current);
        auto& ch = cont->getChildren();
        if (ch.size() == 1 && IsPureFilterGroup(ch[0].get())) {
          current = ch[0].get();
        } else if (ch.size() == 1 && IsMatchingShape(ch[0].get(), ch[0].get())) {
          innermostGroup = cont;
          break;
        } else {
          break;
        }
      }

      if (innermostGroup) {
        auto contentShared = children[i];
        innermostGroup->replaceChild(0, contentShared);
        container->eraseChild(i);
      } else {
        container->eraseChild(filterGroupIdx);
      }
    }
  }
}

void ProcessContainer(SVGContainer* container, const SVGIDMapper& idMapper) {
  // Collect child containers first, then recurse.
  std::vector<SVGContainer*> childContainers;
  for (const auto& child : container->getChildren()) {
    if (child->hasChildren()) {
      childContainers.push_back(static_cast<SVGContainer*>(child.get()));
    }
  }
  for (auto* childContainer : childContainers) {
    ProcessContainer(childContainer, idMapper);
  }

  auto candidates = CollectMergeCandidates(container, idMapper);
  if (!candidates.empty()) {
    ExecuteMerge(container, candidates, idMapper);
  }
}

}  // namespace

void MergeInnerShadowFilters(SVGContainer* root, const SVGIDMapper& idMapper) {
  if (!root) {
    return;
  }
  ProcessContainer(root, idMapper);
}

}  // namespace tgfx
