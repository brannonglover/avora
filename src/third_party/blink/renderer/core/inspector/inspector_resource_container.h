// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_INSPECTOR_INSPECTOR_RESOURCE_CONTAINER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_INSPECTOR_INSPECTOR_RESOURCE_CONTAINER_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/graphics/dom_node_id.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/text/string_hash.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class CSSStyleSheet;
class Document;
class InspectedFrames;
class LocalFrame;

class CORE_EXPORT InspectorResourceContainer final
    : public GarbageCollected<InspectorResourceContainer> {
 public:
  explicit InspectorResourceContainer(InspectedFrames*);
  InspectorResourceContainer(const InspectorResourceContainer&) = delete;
  InspectorResourceContainer& operator=(const InspectorResourceContainer&) =
      delete;
  ~InspectorResourceContainer();
  void Trace(Visitor*) const;

  void DidCommitLoadForLocalFrame(LocalFrame*);

  void StoreStyleSheetContent(const String& url, const String& content);
  bool LoadStyleSheetContent(const String& url, String* content);

  void StoreStyleElementContent(DOMNodeId backend_node_id,
                                const String& content);
  bool LoadStyleElementContent(DOMNodeId backend_node_id, String* content);
  void EraseStyleElementContent(DOMNodeId backend_node_id);

  // Text of an inspector stylesheet (CSS.createStyleSheet). A document can
  // hold several, and all of them share the document as their owner node, so
  // each is keyed by the sheet itself rather than by its owner.
  void StoreInspectorStyleSheetContent(const CSSStyleSheet* sheet,
                                       const String& content);
  bool LoadInspectorStyleSheetContent(const CSSStyleSheet* sheet,
                                      String* content);

  // The document's default inspector stylesheet: the one
  // CSS.createStyleSheet returns without `force`, in every DevTools session.
  // Returns null if there is none, or if it is no longer one of the
  // document's inspector stylesheets.
  CSSStyleSheet* DefaultInspectorStyleSheet(Document* document);
  void SetDefaultInspectorStyleSheet(Document* document, CSSStyleSheet* sheet);
  wtf_size_t DefaultInspectorStyleSheetCountForTesting() const {
    return default_inspector_style_sheets_.size();
  }

 private:
  Member<InspectedFrames> inspected_frames_;
  HashMap<String, String> style_sheet_contents_;
  HashMap<DOMNodeId, String> style_element_contents_;
  // Weak: an entry goes away with its sheet, which goes away with its
  // document.
  HeapHashMap<WeakMember<const CSSStyleSheet>, String>
      inspector_style_sheet_contents_;
  // Weak on both sides: neither a document nor its default sheet is kept
  // alive by being the default.
  HeapHashMap<WeakMember<Document>, WeakMember<CSSStyleSheet>>
      default_inspector_style_sheets_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_INSPECTOR_INSPECTOR_RESOURCE_CONTAINER_H_
