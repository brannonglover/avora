// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/inspector/inspector_resource_container.h"

#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/inspector/inspected_frames.h"

namespace blink {

InspectorResourceContainer::InspectorResourceContainer(
    InspectedFrames* inspected_frames)
    : inspected_frames_(inspected_frames) {}

InspectorResourceContainer::~InspectorResourceContainer() = default;

void InspectorResourceContainer::Trace(Visitor* visitor) const {
  visitor->Trace(inspected_frames_);
  visitor->Trace(inspector_style_sheet_contents_);
  visitor->Trace(default_inspector_style_sheets_);
}

void InspectorResourceContainer::DidCommitLoadForLocalFrame(LocalFrame* frame) {
  if (frame != inspected_frames_->Root())
    return;
  style_sheet_contents_.clear();
  style_element_contents_.clear();
  inspector_style_sheet_contents_.clear();
  default_inspector_style_sheets_.clear();
}

void InspectorResourceContainer::StoreStyleSheetContent(const String& url,
                                                        const String& content) {
  style_sheet_contents_.Set(url, content);
}

bool InspectorResourceContainer::LoadStyleSheetContent(const String& url,
                                                       String* content) {
  if (!style_sheet_contents_.Contains(url))
    return false;
  *content = style_sheet_contents_.at(url);
  return true;
}

void InspectorResourceContainer::StoreStyleElementContent(
    DOMNodeId backend_node_id,
    const String& content) {
  style_element_contents_.Set(backend_node_id, content);
}

bool InspectorResourceContainer::LoadStyleElementContent(
    DOMNodeId backend_node_id,
    String* content) {
  if (!style_element_contents_.Contains(backend_node_id))
    return false;
  *content = style_element_contents_.at(backend_node_id);
  return true;
}

void InspectorResourceContainer::EraseStyleElementContent(
    DOMNodeId backend_node_id) {
  style_element_contents_.erase(backend_node_id);
}

void InspectorResourceContainer::StoreInspectorStyleSheetContent(
    const CSSStyleSheet* sheet,
    const String& content) {
  inspector_style_sheet_contents_.Set(sheet, content);
}

bool InspectorResourceContainer::LoadInspectorStyleSheetContent(
    const CSSStyleSheet* sheet,
    String* content) {
  auto it = inspector_style_sheet_contents_.find(sheet);
  if (it == inspector_style_sheet_contents_.end()) {
    return false;
  }
  *content = it->value;
  return true;
}

CSSStyleSheet* InspectorResourceContainer::DefaultInspectorStyleSheet(
    Document* document) {
  auto it = default_inspector_style_sheets_.find(document);
  if (it == default_inspector_style_sheets_.end()) {
    return nullptr;
  }
  CSSStyleSheet* sheet = it->value.Get();
  // A detached document drops its inspector stylesheets; never hand one back.
  if (!sheet ||
      !document->GetStyleEngine().InspectorStyleSheets().Contains(sheet)) {
    default_inspector_style_sheets_.erase(it);
    return nullptr;
  }
  return sheet;
}

void InspectorResourceContainer::SetDefaultInspectorStyleSheet(
    Document* document,
    CSSStyleSheet* sheet) {
  default_inspector_style_sheets_.Set(document, sheet);
}

}  // namespace blink
