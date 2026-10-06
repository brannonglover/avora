// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/inspector/inspector_css_agent.h"

#include <algorithm>

#include "third_party/blink/renderer/core/css/css_function_rule.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/css_style_declaration.h"
#include "third_party/blink/renderer/core/css/css_style_rule.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/html/html_dialog_element.h"
#include "third_party/blink/renderer/core/html/html_iframe_element.h"
#include "third_party/blink/renderer/core/inspector/inspected_frames.h"
#include "third_party/blink/renderer/core/inspector/inspector_dom_agent.h"
#include "third_party/blink/renderer/core/inspector/inspector_network_agent.h"
#include "third_party/blink/renderer/core/inspector/inspector_resource_container.h"
#include "third_party/blink/renderer/core/inspector/inspector_resource_content_loader.h"
#include "third_party/blink/renderer/core/inspector/inspector_session_state.h"
#include "third_party/blink/renderer/core/inspector/inspector_style_resolver.h"
#include "third_party/blink/renderer/core/inspector/inspector_style_sheet.h"
#include "third_party/blink/renderer/core/inspector/protocol/protocol.h"
#include "third_party/blink/renderer/core/inspector/v8_session_holder.h"
#include "third_party/blink/renderer/core/layout/layout_box_model_object.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

class InspectorCSSAgentTest : public PageTestBase {
 public:
  HeapHashSet<Member<CSSStyleSheet>> CollectAllDocumentStyleSheets() {
    HeapVector<Member<CSSStyleSheet>> sheets;
    InspectorCSSAgent::CollectAllDocumentStyleSheets(&GetDocument(), sheets);

    HeapHashSet<Member<CSSStyleSheet>> sheets_set;
    for (Member<CSSStyleSheet>& sheet : sheets) {
      sheets_set.insert(sheet);
    }
    return sheets_set;
  }

  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
  CollectReferencedFunctionRules(const char* selector) {
    HeapHashSet<Member<CSSStyleSheet>> sheets = CollectAllDocumentStyleSheets();

    Element* e = GetDocument().querySelector(AtomicString(selector),
                                             ASSERT_NO_EXCEPTION);
    CHECK(e);
    InspectorStyleResolver resolver(e, kPseudoIdNone,
                                    /*pseudo_argument=*/g_null_atom);

    HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
        function_rules;
    InspectorCSSAgent::CollectReferencedFunctionRules(
        InspectorCSSAgent::BuildFunctionRuleMap(sheets),
        *resolver.MatchedRules(), function_rules);
    return function_rules;
  }

  InspectorCSSAgent* CreateInspectorCSSAgent() {
    LocalFrame* frame = GetDocument().GetFrame();
    InspectedFrames* inspected_frames =
        MakeGarbageCollected<InspectedFrames>(frame);
    InspectorCSSAgent* agent = MakeGarbageCollected<InspectorCSSAgent>(
        MakeGarbageCollected<InspectorDOMAgent>(
            GetDocument().GetExecutionContext()->GetIsolate(),
            inspected_frames),
        inspected_frames,
        MakeGarbageCollected<InspectorNetworkAgent>(inspected_frames, nullptr),
        MakeGarbageCollected<InspectorResourceContentLoader>(
            GetDocument().GetFrame()),
        MakeGarbageCollected<InspectorResourceContainer>(inspected_frames));
    agent->UpdateActiveStyleSheets(&GetDocument());
    return agent;
  }

  using AtRules = std::unique_ptr<protocol::Array<protocol::CSS::CSSAtRule>>;
  AtRules CollectFontAtRules(const char* selector,
                             std::vector<PseudoId> pseudo_ids) {
    Element* e = GetDocument().querySelector(AtomicString(selector),
                                             ASSERT_NO_EXCEPTION);
    CHECK(e);
    HeapVector<Member<Element>> elements = {e};
    for (PseudoId pseudo_id : pseudo_ids) {
      Element* pseudo_element = e->GetPseudoElement(pseudo_id);
      CHECK(pseudo_element);
      elements.push_back(pseudo_element);
    }
    return CreateInspectorCSSAgent()->FontAtRulesForNodes(elements);
  }

  AtRules CollectFontAtRules(const char* selector) {
    return CollectFontAtRules(selector, {});
  }

  AtRules CollectCounterAtRules(const char* selector) {
    Element* e = GetDocument().querySelector(AtomicString(selector),
                                             ASSERT_NO_EXCEPTION);
    CHECK(e);
    return CreateInspectorCSSAgent()->CounterAtRulesForElement(e);
  }

  CSSStyleRule* GetSingleNestedStyleRule(TreeScope* tree_scope) {
    CHECK_EQ(tree_scope->StyleSheets().length(), 1);
    StyleSheet* sheet = tree_scope->StyleSheets().item(0);
    CHECK(sheet->IsCSSStyleSheet());
    CSSRuleList* css_rules =
        To<CSSStyleSheet>(*sheet).cssRules(ASSERT_NO_EXCEPTION);
    CHECK_EQ(css_rules->length(), 1);
    CSSRule* rule = css_rules->item(0);
    return DynamicTo<CSSStyleRule>(rule);
  }

  CSSStyleRule* GetInnermostStyleRule(CSSStyleSheet* sheet) {
    CSSRuleList* css_rules = sheet->cssRules(ASSERT_NO_EXCEPTION);
    CHECK_EQ(css_rules->length(), 1u);
    CSSRule* rule = css_rules->item(0);

    while (rule->cssRules()->length() > 0) {
      rule = rule->cssRules()->item(0);
      CHECK_LE(css_rules->length(), 1u);
    }
    return DynamicTo<CSSStyleRule>(rule);
  }

  std::unique_ptr<protocol::CSS::CSSRule> BuildObjectForInnermostRule(
      const char* selector) {
    Element* e = GetDocument().querySelector(AtomicString(selector),
                                             ASSERT_NO_EXCEPTION);
    CHECK(e);
    TreeScope* tree_scope = &(e->GetTreeScope());
    CHECK(tree_scope);
    CHECK_EQ(tree_scope->StyleSheets().length(), 1u);
    StyleSheet* sheet = tree_scope->StyleSheets().item(0);
    CHECK(sheet->IsCSSStyleSheet());
    CSSStyleRule* rule = GetInnermostStyleRule(DynamicTo<CSSStyleSheet>(sheet));
    CHECK(rule);
    return CreateInspectorCSSAgent()->BuildObjectForRule(
        rule, e, kPseudoIdNone, /*pseudo_argument=*/g_null_atom, tree_scope);
  }

  CSSFunctionRule* FindFunctionRule(
      const HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>&
          function_rules,
      const ScopedCSSName* scoped_name) {
    auto it = function_rules.find(scoped_name);
    return it != function_rules.end() ? it->value.Get() : nullptr;
  }

  CSSFunctionRule* FindFunctionRule(
      const HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>&
          function_rules,
      const char* name) {
    auto* scoped_name = MakeGarbageCollected<ScopedCSSName>(
        AtomicString(name), /*tree_scope=*/&GetDocument());
    return FindFunctionRule(function_rules, scoped_name);
  }

  String GetComputedStyle(Element* element, CSSPropertyID property_id) {
    const CSSProperty& property = CSSProperty::Get(property_id);
    const ComputedStyle* computed_style = element->GetComputedStyle();
    const LayoutObject* layout_object = element->GetLayoutObject();
    const CSSValue* computed_value =
        property.CSSProperty::CSSValueFromComputedStyle(
            *computed_style, layout_object, /* allow_visited_style */ false,
            CSSValuePhase::kResolvedValue);
    return computed_value->CssText();
  }

  String InspectorResolvePercentageValues(Element* element,
                                          CSSPropertyID property_id,
                                          String value_str) {
    const CSSPropertyName property_name =
        CSSProperty::Get(property_id).GetCSSPropertyName();
    ScopedNullExecutionContext execution_context;
    auto* document =
        Document::CreateForTest(execution_context.GetExecutionContext());
    const CSSValue* value = css_test_helpers::ParseValue(
        *document, "<length-percentage>", value_str);
    return InspectorCSSAgent::ResolvePercentagesValues(element, property_name,
                                                       value, value_str);
  }
};

TEST_F(InspectorCSSAgentTest, NoFunctions) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      #e { width: 1px; }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_TRUE(function_rules.empty());
}

TEST_F(InspectorCSSAgentTest, UnreferencedFunction) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      #e { width: 1px; }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_TRUE(function_rules.empty());
}

TEST_F(InspectorCSSAgentTest, ElementSpecificFunctionReferences) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      #e1 { width: 1px; }
      #e2 { width: --a(); }
    </style>
    <div id=e1></div>
    <div id=e2></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      e1_function_rules = CollectReferencedFunctionRules("#e1");
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      e2_function_rules = CollectReferencedFunctionRules("#e2");
  EXPECT_TRUE(e1_function_rules.empty());
  EXPECT_EQ(1u, e2_function_rules.size());
  EXPECT_TRUE(FindFunctionRule(e2_function_rules, "--a"));
}

TEST_F(InspectorCSSAgentTest, MultipleFunctions_Declaration) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      @function --b() { result: 2px; }
      @function --c() { result: 1000px; }
      #e { width: calc(--a() + --b()); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(2u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
  EXPECT_TRUE(FindFunctionRule(function_rules, "--b"));
  EXPECT_FALSE(FindFunctionRule(function_rules, "--c"));
}

TEST_F(InspectorCSSAgentTest, KeyNameVsFunctionName) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      @function --b() { result: 2px; }
      @function --c() { result: 3px; }
      #e { width: calc(--a() + --b() + --c()); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  // Check that the names held by the keys correspond to names held
  // by the values:
  EXPECT_EQ(3u, function_rules.size());
  ASSERT_TRUE(FindFunctionRule(function_rules, "--a"));
  EXPECT_EQ("--a", FindFunctionRule(function_rules, "--a")->name());
  ASSERT_TRUE(FindFunctionRule(function_rules, "--b"));
  EXPECT_EQ("--b", FindFunctionRule(function_rules, "--b")->name());
  ASSERT_TRUE(FindFunctionRule(function_rules, "--c"));
  EXPECT_EQ("--c", FindFunctionRule(function_rules, "--c")->name());
}

TEST_F(InspectorCSSAgentTest, MultipleFunctions_Rules) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      @function --b() { result: 2px; }
      @function --c() { result: 1000px; }
      #e { width: --a(); }
      #e { height: --b(); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(2u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
  EXPECT_TRUE(FindFunctionRule(function_rules, "--b"));
  EXPECT_FALSE(FindFunctionRule(function_rules, "--c"));
}

TEST_F(InspectorCSSAgentTest, FunctionsInShorthand) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      #e { padding: --a(); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(1u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
}

TEST_F(InspectorCSSAgentTest, DashedFunctionInMedia) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      @media (width) {
        #e { width: --a(); }
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(1u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
}

TEST_F(InspectorCSSAgentTest, DashedFunctionNested) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      #e {
        & {
          width: --a();
        }
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(1u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
}

TEST_F(InspectorCSSAgentTest, TransitiveFunction) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() {
        result: --b();
      }
      @function --b() {
        result: 2px;
      }
      @function --c() {
        result: 1000px;
      }
      #e { width: --a(); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(2u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
  EXPECT_TRUE(FindFunctionRule(function_rules, "--b"));
  EXPECT_FALSE(FindFunctionRule(function_rules, "--c"));
}

TEST_F(InspectorCSSAgentTest, TransitiveFunctionBranches) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() {
        @media (width > 0px) {
          --x: --b();
        }
        @media (width < -9000px) {
          /* Branch not taken, but referenced functions are still relevant. */
          --x: --c();
        }
        result: var(--x);
      }
      @function --b() {
        result: 2px;
      }
      @function --c() {
        result: 3px;
      }
      @function --d() {
        result: 3px;
      }
      #e { width: --a(); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(3u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
  EXPECT_TRUE(FindFunctionRule(function_rules, "--b"));
  EXPECT_TRUE(FindFunctionRule(function_rules, "--c"));
  EXPECT_FALSE(FindFunctionRule(function_rules, "--d"));
}

TEST_F(InspectorCSSAgentTest, DashedFunctionDedup) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      #e { left: --a(); }
      div { top: --a(); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  // There should only be one entry, despite --a() appearing twice.
  EXPECT_EQ(1u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
}

TEST_F(InspectorCSSAgentTest, DashedFunctionUnknown) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() { result: 1px; }
      #e { left: --unknown(); right: --a(); }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#e");
  EXPECT_EQ(1u, function_rules.size());
  EXPECT_TRUE(FindFunctionRule(function_rules, "--a"));
  // The presence of a reference to a function that does not exist
  // should not cause a crash.
}

TEST_F(InspectorCSSAgentTest, SameFunctionNamesAcrossShadows) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @function --a() {
        result: 10px;
      }
      @function --b() {
        result: --a(); /* Outer --a() */
      }
    </style>
    <div id=host>
      <template shadowrootmode=open>
        <style>
          @function --a() {
            result: --b();
          }
          :host {
             width: --a(); /* Inner --a() */
          }
        </style>
      </template>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
      function_rules = CollectReferencedFunctionRules("#host");
  EXPECT_EQ(3u, function_rules.size());

  Element* host = GetDocument().getElementById(AtomicString("host"));
  ASSERT_TRUE(host);
  const TreeScope* document_scope = &GetDocument();
  const TreeScope* inner_scope = host->GetShadowRoot();
  ASSERT_TRUE(inner_scope);

  CSSFunctionRule* inner_a = FindFunctionRule(
      function_rules,
      MakeGarbageCollected<ScopedCSSName>(AtomicString("--a"), inner_scope));
  CSSFunctionRule* outer_a = FindFunctionRule(
      function_rules,
      MakeGarbageCollected<ScopedCSSName>(AtomicString("--a"), document_scope));
  CSSFunctionRule* outer_b = FindFunctionRule(
      function_rules,
      MakeGarbageCollected<ScopedCSSName>(AtomicString("--b"), inner_scope));

  EXPECT_TRUE(inner_a);
  EXPECT_TRUE(outer_a);
  EXPECT_TRUE(outer_b);
  EXPECT_NE(inner_a, outer_a);

  // `function_rules` maps invocations to the corresponding invoked functions,
  // and there was no invocation of --b() in the outer scope.
  EXPECT_FALSE(FindFunctionRule(
      function_rules, MakeGarbageCollected<ScopedCSSName>(AtomicString("--b"),
                                                          document_scope)));
}

TEST_F(InspectorCSSAgentTest, GetFontFaceRule) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-face {
        font-family: ExampleFontForTest;
        src: url(//nonexistent.ttf);
      }
      #e {
        font-family: ExampleFontForTest;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFace);
  EXPECT_FALSE(rules->at(0)->getSubsection());
  EXPECT_FALSE(rules->at(0)->getName());
  EXPECT_GE(rules->at(0)->getStyle()->getCssProperties()->size(), 2u);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getName(),
            "font-family");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getValue(),
            "ExampleFontForTest");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getName(),
            "src");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getValue(),
            "url(//nonexistent.ttf)");
}

TEST_F(InspectorCSSAgentTest, GetFontFaceRuleNoMatch) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-face {
        font-family: ExampleFontForTest;
        src: url(//nonexistent.ttf);
      }
      #e {
        font-family: Papyrus;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(0u, rules->size());
}

TEST_F(InspectorCSSAgentTest, GetFontFaceRuleFromPseudoElement) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-face {
        font-family: ExampleFontForTest;
        src: url(//nonexistent.ttf);
      }
      #e {
        font-family: Papyrus;
      }
      #e::before {
        content: "before";
        font-family: ExampleFontForTest;
      }
      #e::after {
        content: "after";
        font-family: ExampleFontForTest;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules(
      "#e", {PseudoId::kPseudoIdBefore, PseudoId::kPseudoIdAfter});
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFace);
  EXPECT_FALSE(rules->at(0)->getSubsection());
  EXPECT_FALSE(rules->at(0)->getName());
  EXPECT_GE(rules->at(0)->getStyle()->getCssProperties()->size(), 2u);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getName(),
            "font-family");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getValue(),
            "ExampleFontForTest");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getName(),
            "src");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getValue(),
            "url(//nonexistent.ttf)");
}

TEST_F(InspectorCSSAgentTest, GetFontPaletteValuesRule) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-palette-values --palette {
        font-family: Bixa;
        override-colors: 0 red;
      }
      #e {
        font-family: Bixa;
        font-palette: --palette;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontPaletteValues);
  EXPECT_FALSE(rules->at(0)->getSubsection());
  EXPECT_TRUE(rules->at(0)->getName());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "--palette");
  // The backend currently reports these twice, once with source info and once
  // without.
  EXPECT_GE(rules->at(0)->getStyle()->getCssProperties()->size(), 2u);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getName(),
            "font-family");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getValue(),
            "Bixa");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getName(),
            "override-colors");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getValue(),
            "0 red");
}

TEST_F(InspectorCSSAgentTest, GetCounterStyleRuleSimple) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @counter-style --my-style {
        system: cyclic;
        symbols: 'a' 'b' 'c';
      }
      #e {
        display: list-item;
        list-style-type: --my-style;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectCounterAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::CounterStyle);
  EXPECT_TRUE(rules->at(0)->getName());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "--my-style");
  EXPECT_GE(rules->at(0)->getStyle()->getCssProperties()->size(), 2u);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getName(),
            "system");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getValue(),
            "cyclic");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getName(),
            "symbols");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getValue(),
            "'a' 'b' 'c'");
}

TEST_F(InspectorCSSAgentTest, GetCounterStyleRuleTwoFallbacks) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @counter-style style-b {
        system: cyclic;
        symbols: 'x' 'y';
      }
      @counter-style style-a {
        system: cyclic;
        symbols: 'a' 'b';
        fallback: style-b;
      }
      #e {
        display: list-item;
        list-style-type: style-a;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectCounterAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(2u, rules->size());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "style-a");
  EXPECT_EQ(rules->at(1)->getName()->getText(), "style-b");
}

TEST_F(InspectorCSSAgentTest, GetCounterStyleRuleCycle) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @counter-style --my-style {
        system: cyclic;
        symbols: 'a' 'b';
        fallback: --my-style;
      }
      #e {
        display: list-item;
        list-style-type: --my-style;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectCounterAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "--my-style");
}

TEST_F(InspectorCSSAgentTest, GetCounterStyleRuleNonExistentFallback) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @counter-style --my-style {
        system: cyclic;
        symbols: 'a' 'b';
        fallback: --non-existent-style;
      }
      #e {
        display: list-item;
        list-style-type: --my-style;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectCounterAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "--my-style");
}

TEST_F(InspectorCSSAgentTest, GetCounterStyleRuleBuiltInFallback) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @counter-style --my-style {
        system: cyclic;
        symbols: 'a' 'b';
        fallback: decimal;
      }
      #e {
        display: list-item;
        list-style-type: --my-style;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectCounterAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "--my-style");
}

TEST_F(InspectorCSSAgentTest, GetCounterStyleRuleSymbolsFunctionHasNoAtRule) {
  ScopedCSSCounterStyleSymbolsFunctionForTest scoped_feature(true);
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      #e {
        display: list-item;
        list-style-type: symbols('a' 'b' 'c');
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  // An anonymous symbols() counter style has no name, so there is no
  // corresponding @counter-style at-rule to report to DevTools.
  AtRules rules = CollectCounterAtRules("#e");
  EXPECT_FALSE(rules);
}

TEST_F(InspectorCSSAgentTest, GetFontPaletteValuesRuleNoMatchPalette) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-palette-values --palette {
        font-family: Bixa;
        override-colors: 0 red;
      }
      #e {
        font-family: Bixa;
        font-palette: --other-palette;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(0u, rules->size());
}

TEST_F(InspectorCSSAgentTest, GetFontPaletteValuesRuleNoMatchFont) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-palette-values --palette {
        font-family: Bixa;
        override-colors: 0 red;
      }
      #e {
        font-family: Papyrus;
        font-palette: --palette;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(0u, rules->size());
}

TEST_F(InspectorCSSAgentTest, GetFontPaletteValuesRuleFromPseudoElement) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-palette-values --palette {
        font-family: Bixa;
        override-colors: 0 red;
      }
      #e {
        font-family: Bixa;
      }
      #e::before {
        content: "before";
        font-palette: --palette;
      }
      #e::after {
        content: "after";
        font-palette: --palette;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules(
      "#e", {PseudoId::kPseudoIdBefore, PseudoId::kPseudoIdAfter});
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontPaletteValues);
  EXPECT_FALSE(rules->at(0)->getSubsection());
  EXPECT_TRUE(rules->at(0)->getName());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "--palette");
  // The backend currently reports these twice, once with source info and once
  // without.
  EXPECT_GE(rules->at(0)->getStyle()->getCssProperties()->size(), 2u);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getName(),
            "font-family");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getValue(),
            "Bixa");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getName(),
            "override-colors");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(1)->getValue(),
            "0 red");
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleSwash) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @swash {
          fancy: 1;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: swash(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Swash);
  EXPECT_TRUE(rules->at(0)->getName());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "Bixa");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->size(), 1u);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getName(),
            "fancy");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getValue(),
            "1");
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleNoMatchFont) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @swash {
          fancy: 1;
        }
      }
      #e {
        font-family: Papyrus;
        font-variant-alternates: swash(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(0u, rules->size());
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleNoMatchFeature) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @swash {
          fancy: 1;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: styleset(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(0u, rules->size());
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleAnnotation) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @annotation {
          fancy: 1;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: annotation(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Annotation);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->size(), 1u);
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleOrnaments) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @ornaments {
          fancy: 1;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: ornaments(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Ornaments);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->size(), 1u);
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleStylistic) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @stylistic {
          fancy: 1;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: stylistic(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Stylistic);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->size(), 1u);
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleStyleset) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @styleset {
          fancy: 1;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: styleset(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Styleset);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->size(), 1u);
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleCharacterVariant) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @character-variant {
          fancy: 1;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: character-variant(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::CharacterVariant);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->size(), 1u);
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleMultipleFamilies) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa, Papyrus {
        @character-variant {
          fancy: 1;
        }
      }
      #e {
        font-family: Papyrus;
        font-variant-alternates: character-variant(fancy);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(1u, rules->size());
  EXPECT_EQ(rules->at(0)->getType(),
            protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::CharacterVariant);
  EXPECT_TRUE(rules->at(0)->getName());
  EXPECT_EQ(rules->at(0)->getName()->getText(), "Bixa, Papyrus");
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->size(), 1u);
}

TEST_F(InspectorCSSAgentTest, GetFontFeatureValuesRuleMultipleFeatures) {
  GetDocument().body()->SetHTMLUnsafeWithoutTrustedTypes(R"HTML(
    <style>
      @font-feature-values Bixa {
        @swash {
          fancy: 1;
        }
        @ornaments {
          nope: 3;
        }
      }
      @font-feature-values Bixa {
        @swash {
          swishy: 4;
        }
        @annotation {
          extra: 5;
        }
      }
      #e {
        font-family: Bixa;
        font-variant-alternates: swash(swishy) historical-forms annotation(extra) character-variant(oops);
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  AtRules rules = CollectFontAtRules("#e");
  EXPECT_TRUE(rules);
  EXPECT_EQ(3u, rules->size());
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(rules->at(i)->getType(),
              protocol::CSS::CSSAtRule::TypeEnum::FontFeatureValues);
    EXPECT_TRUE(rules->at(i)->getName());
    EXPECT_EQ(rules->at(i)->getName()->getText(), "Bixa");
    EXPECT_EQ(rules->at(i)->getStyle()->getCssProperties()->size(), 1u);
  }
  EXPECT_EQ(rules->at(0)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Swash);
  EXPECT_EQ(rules->at(0)->getStyle()->getCssProperties()->at(0)->getName(),
            "fancy");
  EXPECT_EQ(rules->at(1)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Swash);
  EXPECT_EQ(rules->at(1)->getStyle()->getCssProperties()->at(0)->getName(),
            "swishy");
  EXPECT_EQ(rules->at(2)->getSubsection(),
            protocol::CSS::CSSAtRule::SubsectionEnum::Annotation);
  EXPECT_EQ(rules->at(2)->getStyle()->getCssProperties()->at(0)->getName(),
            "extra");
}

TEST_F(InspectorCSSAgentTest, BuildSimpleRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      #e {
        color: red;
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  EXPECT_FALSE(rule->getNestingSelectors());
  EXPECT_EQ(rule->getRuleTypes()->size(), 0u);
  EXPECT_EQ(rule->getMedia()->size(), 0u);
  EXPECT_EQ(rule->getSupports()->size(), 0u);
  EXPECT_EQ(rule->getContainerQueries()->size(), 0u);
  EXPECT_EQ(rule->getScopes()->size(), 0u);
  EXPECT_EQ(rule->getLayers()->size(), 0u);
  EXPECT_EQ(rule->getStartingStyles()->size(), 0u);
}

TEST_F(InspectorCSSAgentTest, BuildNestedStyleRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      div {
        #e {
          color: red;
        }
      }
    </style>
    <div><div id=e></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "& #e");
  ASSERT_TRUE(rule->getNestingSelectors());
  EXPECT_EQ(rule->getNestingSelectors()->size(), 1u);
  EXPECT_EQ(rule->getNestingSelectors()->at(0), "div");
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::StyleRule);
}

TEST_F(InspectorCSSAgentTest, BuildNestedMediaRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @media screen and (min-width: 1px) {
        #e {
          color: red;
        }
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  ASSERT_EQ(rule->getMedia()->size(), 1u);
  EXPECT_EQ(rule->getMedia()->at(0)->getText(), "screen and (min-width: 1px)");
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::MediaRule);
}

TEST_F(InspectorCSSAgentTest, BuildNestedSupportsRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @supports (display: grid) {
        #e {
          color: red;
        }
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  ASSERT_EQ(rule->getSupports()->size(), 1u);
  EXPECT_EQ(rule->getSupports()->at(0)->getText(), "(display: grid)");
  EXPECT_TRUE(rule->getSupports()->at(0)->getActive());
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::SupportsRule);
}

TEST_F(InspectorCSSAgentTest, BuildNestedContainerRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @container (min-width: 1px) {
        #e {
          color: red;
        }
      }
    </style>
    <div style="container-type: inline-size;"><div id=e></div></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  ASSERT_EQ(rule->getContainerQueries()->size(), 1u);
  EXPECT_EQ(rule->getContainerQueries()->at(0)->getText(), "(min-width: 1px)");
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::ContainerRule);
}

TEST_F(InspectorCSSAgentTest, BuildNestedLayerRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @layer my-layer {
        #e {
          color: red;
        }
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  ASSERT_EQ(rule->getLayers()->size(), 1u);
  EXPECT_EQ(rule->getLayers()->at(0)->getText(), "my-layer");
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::LayerRule);
}

TEST_F(InspectorCSSAgentTest, BuildNestedScopeRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @scope (.scope) {
        #e {
          color: red;
        }
      }
    </style>
    <div class=scope>
        <div id=e></div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  ASSERT_EQ(rule->getScopes()->size(), 1u);
  EXPECT_EQ(rule->getScopes()->at(0)->getText(), "(.scope)");
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::ScopeRule);
}

TEST_F(InspectorCSSAgentTest, BuildNestedStartingStyleRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @starting-style {
        #e {
          color: red;
        }
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  ASSERT_EQ(rule->getStartingStyles()->size(), 1u);
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::StartingStyleRule);
}

TEST_F(InspectorCSSAgentTest, BuildNestedNavigationRule) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      @navigation not (at: url-pattern("http:*xxx*")) {
        #e {
          color: red;
        }
      }
    </style>
    <div id=e></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  std::unique_ptr<protocol::CSS::CSSRule> rule =
      BuildObjectForInnermostRule("#e");
  EXPECT_TRUE(rule);
  EXPECT_EQ(rule->getSelectorList()->getText(), "#e");
  ASSERT_EQ(rule->getNavigations()->size(), 1u);
  EXPECT_EQ(rule->getNavigations()->at(0)->getText(),
            "not (at: url-pattern(\"http:*xxx*\"))");
  EXPECT_TRUE(rule->getNavigations()->at(0)->getActive());
  ASSERT_EQ(rule->getRuleTypes()->size(), 1u);
  EXPECT_EQ(rule->getRuleTypes()->at(0),
            protocol::CSS::CSSRuleTypeEnum::NavigationRule);
}

const CSSPropertyID DirectionAwareConverterTestData[] = {
    // clang-format off
  CSSPropertyID::kWidth,
  CSSPropertyID::kHeight,
  CSSPropertyID::kMarginLeft,
  CSSPropertyID::kMarginTop,
  CSSPropertyID::kMarginRight,
  CSSPropertyID::kMarginBottom,
  CSSPropertyID::kPaddingLeft,
  CSSPropertyID::kPaddingTop,
  CSSPropertyID::kPaddingRight,
  CSSPropertyID::kPaddingBottom,
  CSSPropertyID::kLeft,
  CSSPropertyID::kTop,
  CSSPropertyID::kRight,
  CSSPropertyID::kBottom,
    // clang-format on
};

class PercentageResolutionTest
    : public InspectorCSSAgentTest,
      public testing::WithParamInterface<CSSPropertyID> {};

INSTANTIATE_TEST_SUITE_P(InspectorCSSAgentTest,
                         PercentageResolutionTest,
                         testing::ValuesIn(DirectionAwareConverterTestData));

TEST_P(PercentageResolutionTest, ResolvePercentagesSimple) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      #outer {
        width: 100px;
        height: 300px;
      }
    </style>
    <div id=outer>
    </div>
  )HTML");

  String value("calc(1px + 10%)");

  CSSPropertyID property_id = GetParam();
  AtomicString property_name =
      CSSProperty::Get(property_id).GetCSSPropertyName().ToAtomicString();

  StringBuilder html_string;
  html_string.Append("<div id=inner style=\"position: absolute; ");
  html_string.Append(property_name);
  html_string.Append(": ");
  html_string.Append(value);
  html_string.Append(";\"></div>");
  GetElementById("outer")->SetInnerHTMLWithoutTrustedTypes(
      html_string.ToString());

  UpdateAllLifecyclePhasesForTest();

  Element* element = GetElementById("inner");
  String expected = GetComputedStyle(element, property_id);
  String actual = InspectorResolvePercentageValues(element, property_id, value);
  EXPECT_EQ(actual, expected);
}

TEST_P(PercentageResolutionTest, ResolvePercentagesEffectiveZoom) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      #outer {
        width: 100px;
        height: 300px;
        zoom: 3;
      }
    </style>
    <div id=outer>
    </div>
  )HTML");

  String value("10%");

  CSSPropertyID property_id = GetParam();
  AtomicString property_name =
      CSSProperty::Get(property_id).GetCSSPropertyName().ToAtomicString();

  StringBuilder html_string;
  html_string.Append("<div id=inner style=\"position: absolute; ");
  html_string.Append(property_name);
  html_string.Append(": ");
  html_string.Append(value);
  html_string.Append(";\"></div>");
  GetElementById("outer")->SetInnerHTMLWithoutTrustedTypes(
      html_string.ToString());

  UpdateAllLifecyclePhasesForTest();

  Element* element = GetElementById("inner");
  String expected = GetComputedStyle(element, property_id);
  String actual = InspectorResolvePercentageValues(element, property_id, value);
  EXPECT_EQ(actual, expected);
}

TEST_F(InspectorCSSAgentTest, ResolvePercentagesSizingProperties) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      #outer {
        width: 100px;
        height: 300px
      }
      #inner {
        width: calc(1px + 10%);
        height: calc(1px + 10%);
      }
    </style>
    <div id=outer>
      <div id=inner></div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* element = GetElementById("inner");
  String value("calc(1px + 10%)");

  String expected_inline = GetComputedStyle(element, CSSPropertyID::kWidth);
  String expected_block = GetComputedStyle(element, CSSPropertyID::kHeight);

  EXPECT_EQ(InspectorResolvePercentageValues(element, CSSPropertyID::kMinWidth,
                                             value),
            expected_inline);
  EXPECT_EQ(InspectorResolvePercentageValues(element, CSSPropertyID::kMaxWidth,
                                             value),
            expected_inline);
  EXPECT_EQ(InspectorResolvePercentageValues(element, CSSPropertyID::kMinHeight,
                                             value),
            expected_block);
  EXPECT_EQ(InspectorResolvePercentageValues(element, CSSPropertyID::kMaxHeight,
                                             value),
            expected_block);
}

TEST_F(InspectorCSSAgentTest, ResolvePercentagesAnchorPositioning) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      #cb {
        width: 300px;
        height: 150px;
        position: relative;
      }
      #anchor {
        position: absolute;
        left: 50px;
        top: 20px;
        anchor-name: --a;
        width: 50px;
        height: 50px;
      }
      #anchored {
        position-anchor: --a;
        position-area: center right;
        position: absolute;
        width: 10%;
        height: 10%;
      }
    </style>
    <div id=cb>
      <div id=anchor>
        Anchor
      </div>
      <div id=anchored>
      </div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* element = GetElementById("anchored");
  String value("10%");

  String expected_inline = GetComputedStyle(element, CSSPropertyID::kWidth);
  String expected_block = GetComputedStyle(element, CSSPropertyID::kHeight);

  EXPECT_EQ(
      InspectorResolvePercentageValues(element, CSSPropertyID::kWidth, value),
      expected_inline);
  EXPECT_EQ(
      InspectorResolvePercentageValues(element, CSSPropertyID::kWidth, value),
      "20px");
  EXPECT_EQ(
      InspectorResolvePercentageValues(element, CSSPropertyID::kHeight, value),
      expected_block);
  EXPECT_EQ(
      InspectorResolvePercentageValues(element, CSSPropertyID::kHeight, value),
      "5px");
}

TEST_F(InspectorCSSAgentTest, ResolvePercentagesDisplayTable) {
  GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <style>
      .table {
        display: table;
        width: 100px;
        height: 300px;
      }
      #column1 {
        display: table-column;
      }
      #column2 {
        display: table-column;
      }
      .row {
        display: table-row;
      }
      .cell {
        display: table-cell;
      }
    </style>

    <div class="table">
        <div id="column1"></div>
        <div id="column2"></div>
        <div class="row">
          <div class="cell">row 1, cell 1</div>
          <div class="cell">row 1, cell 2</div>
        </div>
        <div class="row">
          <div class="cell">row 2, cell 1</div>
          <div class="cell">row 2, cell 2</div>
        </div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* element = GetElementById("column1");
  String value("10%");

  EXPECT_EQ(
      InspectorResolvePercentageValues(element, CSSPropertyID::kWidth, value),
      value);
  EXPECT_EQ(
      InspectorResolvePercentageValues(element, CSSPropertyID::kHeight, value),
      value);
}

namespace {

class RecordingFrontendChannel : public protocol::FrontendChannel {
 public:
  void SendProtocolResponse(
      int call_id,
      std::unique_ptr<protocol::Serializable> message) override {}
  void SendProtocolNotification(
      std::unique_ptr<protocol::Serializable> message) override {}
  void FlushProtocolNotifications() override {}
};

class RecordingEnableCallback : public protocol::CSS::Backend::EnableCallback {
 public:
  void sendSuccess() override { ran_ = true; }
  void sendFailure(const protocol::Response&) override {
    ran_ = true;
    failed_ = true;
  }
  void fallThrough() override { ran_ = true; }
  bool ran() const { return ran_; }
  bool failed() const { return failed_; }

 private:
  bool ran_ = false;
  bool failed_ = false;
};

class LayoutRelationshipSession {
 public:
  explicit LayoutRelationshipSession(PageTestBase* test) : test_(test) {
    LocalFrame* frame = test_->GetDocument().GetFrame();
    dispatcher_ =
        std::make_unique<protocol::UberDispatcher>(&frontend_channel_);
    session_state_ = std::make_unique<InspectorSessionState>(
        mojom::blink::DevToolsSessionState::New());
    inspected_frames_ = MakeGarbageCollected<InspectedFrames>(frame);
    dom_agent_ = MakeGarbageCollected<InspectorDOMAgent>(
        test_->GetDocument().GetExecutionContext()->GetIsolate(),
        inspected_frames_);
    auto* network_agent = MakeGarbageCollected<InspectorNetworkAgent>(
        inspected_frames_, nullptr);
    auto* resource_loader =
        MakeGarbageCollected<InspectorResourceContentLoader>(frame);
    auto* resource_container =
        MakeGarbageCollected<InspectorResourceContainer>(inspected_frames_);
    css_agent_ = MakeGarbageCollected<InspectorCSSAgent>(
        dom_agent_, inspected_frames_, network_agent, resource_loader,
        resource_container);
    dom_agent_->Init(frame->GetProbeSink(), dispatcher_.get(),
                     session_state_.get(), V8SessionHolder());
    css_agent_->Init(frame->GetProbeSink(), dispatcher_.get(),
                     session_state_.get(), V8SessionHolder());
    CHECK(dom_agent_->enable(std::nullopt).IsSuccess());
    std::unique_ptr<protocol::DOM::Node> root;
    CHECK(dom_agent_->getDocument(-1, true, &root).IsSuccess());
    css_agent_->enable(std::make_unique<RecordingEnableCallback>());
  }

  ~LayoutRelationshipSession() {
    if (css_agent_) {
      css_agent_->Dispose();
    }
    if (dom_agent_) {
      dom_agent_->Dispose();
    }
  }

  protocol::CSS::LayoutRelationships* Query(Element* element) {
    int node_id = dom_agent_->BoundNodeId(element);
    CHECK(node_id);
    relationships_.reset();
    protocol::Response response =
        css_agent_->getLayoutRelationshipsForNode(node_id, &relationships_);
    CHECK(response.IsSuccess());
    return relationships_.get();
  }

  InspectorDOMAgent* DomAgent() const { return dom_agent_.Get(); }

 private:
  PageTestBase* test_;
  RecordingFrontendChannel frontend_channel_;
  std::unique_ptr<protocol::UberDispatcher> dispatcher_;
  std::unique_ptr<InspectorSessionState> session_state_;
  Persistent<InspectedFrames> inspected_frames_;
  Persistent<InspectorDOMAgent> dom_agent_;
  Persistent<InspectorCSSAgent> css_agent_;
  std::unique_ptr<protocol::CSS::LayoutRelationships> relationships_;
};

void ExpectContainingBlock(protocol::CSS::LayoutRelationshipTarget* target,
                           const LayoutObject* blink,
                           InspectorDOMAgent* dom_agent) {
  ASSERT_TRUE(target);
  ASSERT_TRUE(blink);
  if (blink->IsLayoutView()) {
    EXPECT_EQ(target->getKind(), "initial-containing-block");
    EXPECT_FALSE(target->hasNodeId());
    return;
  }
  if (blink->IsAnonymous() || !DynamicTo<Element>(blink->GetNode())) {
    EXPECT_EQ(target->getKind(), "anonymous");
    EXPECT_FALSE(target->hasNodeId());
    return;
  }
  EXPECT_EQ(target->getKind(), "element");
  EXPECT_EQ(target->getNodeId(0), dom_agent->BoundNodeId(blink->GetNode()));
  EXPECT_NE(target->getNodeId(0), 0);
}

void ExpectScrollContainer(protocol::CSS::LayoutRelationshipTarget* target,
                           const LayoutBox* blink,
                           bool fixed_to_viewport,
                           InspectorDOMAgent* dom_agent) {
  ASSERT_TRUE(target);
  EXPECT_EQ(target->getFixedToViewport(false), fixed_to_viewport);
  if (!blink || blink->IsLayoutView()) {
    EXPECT_EQ(target->getKind(), "document");
    EXPECT_FALSE(target->hasNodeId());
    return;
  }
  EXPECT_EQ(target->getKind(), "element");
  EXPECT_EQ(target->getNodeId(0), dom_agent->BoundNodeId(blink->GetNode()));
  EXPECT_NE(target->getNodeId(0), 0);
}

const LayoutObject* BlinkContainingBlock(const LayoutObject& layout) {
  LayoutObject* container = nullptr;
  if (layout.StyleRef().GetPosition() == EPosition::kAbsolute) {
    container = layout.ContainerForAbsolutePosition();
  } else if (layout.StyleRef().GetPosition() == EPosition::kFixed) {
    container = layout.ContainerForFixedPosition();
  } else {
    return nullptr;
  }
  return container ? container->InclusiveContainingBlock() : nullptr;
}

void BlinkScrollContainer(const LayoutObject& layout,
                          const LayoutBox** box,
                          bool* fixed_to_viewport) {
  *box = nullptr;
  *fixed_to_viewport = false;
  const PaintLayer* enclosing = layout.EnclosingLayer();
  if (!enclosing) {
    return;
  }
  const LayoutBox* enclosing_box = enclosing->GetLayoutBox();
  const PaintLayer* scroll_layer = nullptr;
  if (enclosing_box && enclosing_box != &layout &&
      enclosing_box->IsScrollContainer()) {
    scroll_layer = enclosing;
    *fixed_to_viewport =
        layout.IsFixedPositioned() && enclosing->IsRootLayer();
  } else {
    scroll_layer = enclosing->ContainingScrollContainerLayer(fixed_to_viewport);
  }
  *box = scroll_layer ? scroll_layer->GetLayoutBox() : nullptr;
}

void ExpectRelationshipsMatchBlink(protocol::CSS::LayoutRelationships* reported,
                                   Element* element,
                                   InspectorDOMAgent* dom_agent) {
  LayoutObject* layout = element->GetLayoutObject();
  ASSERT_TRUE(layout);
  if (const LayoutObject* containing_block = BlinkContainingBlock(*layout)) {
    ASSERT_TRUE(reported->hasContainingBlock());
    ExpectContainingBlock(reported->getContainingBlock(nullptr),
                          containing_block, dom_agent);
  } else {
    EXPECT_FALSE(reported->hasContainingBlock());
  }

  const LayoutBox* scroll_box = nullptr;
  bool scroll_fixed_to_viewport = false;
  BlinkScrollContainer(*layout, &scroll_box, &scroll_fixed_to_viewport);
  if (scroll_box || scroll_fixed_to_viewport) {
    ASSERT_TRUE(reported->hasScrollContainer());
    ExpectScrollContainer(reported->getScrollContainer(nullptr), scroll_box,
                          scroll_fixed_to_viewport, dom_agent);
  } else {
    EXPECT_FALSE(reported->hasScrollContainer());
  }

  const PhysicalAxes axes =
      LayoutBoxModelObject::StickyConstrainedAxes(layout->StyleRef());
  if (!axes) {
    EXPECT_FALSE(reported->hasStickyScrollContainers());
    return;
  }
  ASSERT_TRUE(reported->hasStickyScrollContainers());
  protocol::CSS::StickyScrollContainers* sticky =
      reported->getStickyScrollContainers(nullptr);
  ASSERT_TRUE(sticky);
  if (axes & kPhysicalAxesHorizontal) {
    const LayoutBox* horizontal =
        layout->ContainingScrollContainer(PhysicalAxis::kHorizontal);
    ASSERT_TRUE(horizontal);
    ASSERT_TRUE(sticky->hasHorizontal());
    bool axis_fixed = horizontal->IsLayoutView() &&
                      (!scroll_box || scroll_box->IsLayoutView()) &&
                      scroll_fixed_to_viewport;
    ExpectScrollContainer(sticky->getHorizontal(nullptr), horizontal,
                          axis_fixed, dom_agent);
  } else {
    EXPECT_FALSE(sticky->hasHorizontal());
  }
  if (axes & kPhysicalAxesVertical) {
    const LayoutBox* vertical =
        layout->ContainingScrollContainer(PhysicalAxis::kVertical);
    ASSERT_TRUE(vertical);
    ASSERT_TRUE(sticky->hasVertical());
    bool axis_fixed = vertical->IsLayoutView() &&
                      (!scroll_box || scroll_box->IsLayoutView()) &&
                      scroll_fixed_to_viewport;
    ExpectScrollContainer(sticky->getVertical(nullptr), vertical, axis_fixed,
                          dom_agent);
  } else {
    EXPECT_FALSE(sticky->hasVertical());
  }
}

int NodeId(InspectorDOMAgent* dom_agent, Element* element) {
  return dom_agent->BoundNodeId(element);
}

}  // namespace

TEST_F(InspectorCSSAgentTest, LayoutRelationshipsFollowBlink) {
  ScopedSingleAxisScrollContainersForTest single_axis(true);
  SetHtmlInnerHTML(R"HTML(
    <div id="abs-relative" style="position:relative">
      <div id="abs-static">
        <div id="abs-child" style="position:absolute"></div>
      </div>
    </div>
    <div id="abs-transform" style="transform:translateX(0)">
      <div id="abs-transform-child" style="position:absolute"></div>
    </div>
    <div id="abs-filter" style="filter:blur(1px)">
      <div id="abs-filter-child" style="position:absolute"></div>
    </div>
    <div id="abs-perspective" style="perspective:100px">
      <div id="abs-perspective-child" style="position:absolute"></div>
    </div>
    <div id="abs-contain" style="contain:paint">
      <div id="abs-contain-child" style="position:absolute"></div>
    </div>
    <div id="abs-will" style="will-change:transform">
      <div id="abs-will-child" style="position:absolute"></div>
    </div>
    <div id="abs-contents-rel" style="position:relative">
      <div id="abs-contents" style="display:contents">
        <div id="abs-contents-child" style="position:absolute"></div>
      </div>
    </div>
    <div id="abs-icb" style="position:absolute"></div>
    <fieldset id="fieldset" style="position:relative">
      <div id="fieldset-abs" style="position:absolute"></div>
    </fieldset>
    <div id="fixed-plain" style="position:fixed"></div>
    <div id="fixed-relative" style="position:relative">
      <div id="fixed-in-relative" style="position:fixed"></div>
    </div>
    <div id="fixed-transform" style="transform:translateZ(0)">
      <div id="fixed-in-transform" style="position:fixed"></div>
    </div>
    <div id="fixed-filter" style="filter:blur(1px)">
      <div id="fixed-in-filter" style="position:fixed"></div>
    </div>
    <div id="fixed-perspective" style="perspective:100px">
      <div id="fixed-in-perspective" style="position:fixed"></div>
    </div>
    <div id="dialog-transform" style="transform:translateZ(0)">
      <dialog id="top-dialog">d</dialog>
    </div>
    <div id="host"></div>
    <div id="scroll-auto" style="overflow:auto;width:40px;height:40px">
      <div id="scroll-auto-child" style="height:10px">a</div>
    </div>
    <div id="scroll-auto-empty" style="overflow:auto;width:40px;height:40px">
      <div id="scroll-auto-empty-child">e</div>
    </div>
    <div id="scroll-hidden" style="overflow:hidden;width:40px;height:40px">
      <div id="scroll-hidden-child" style="height:200px">h</div>
    </div>
    <div id="scroll-clip" style="overflow:clip;width:40px;height:40px">
      <div id="scroll-clip-child" style="height:200px">c</div>
    </div>
    <div id="scroll-outer" style="overflow:auto;width:80px;height:80px">
      <div id="scroll-inner" style="overflow:auto;width:60px;height:60px">
        <div id="scroll-nested-child" style="height:200px">n</div>
      </div>
    </div>
    <div id="scroll-transformed" style="transform:translateZ(0);overflow:auto;width:40px;height:40px">
      <div id="scroll-transformed-child" style="height:200px">t</div>
    </div>
    <div id="in-flow">flow</div>
    <div id="outer-y" style="overflow-x:clip;overflow-y:scroll;width:80px;height:40px">
      <div id="inner-x" style="overflow-x:scroll;overflow-y:clip;width:70px;height:120px">
        <div id="sticky-split" style="position:sticky;top:8px;left:12px">s</div>
        <div style="width:300px;height:300px"></div>
      </div>
    </div>
    <div id="outer-x" style="overflow-y:clip;overflow-x:scroll;width:40px;height:80px">
      <div id="inner-y" style="overflow-y:scroll;overflow-x:clip;width:120px;height:70px">
        <div id="sticky-rev" style="position:sticky;top:8px;left:12px">r</div>
        <div style="width:300px;height:300px"></div>
      </div>
    </div>
    <div id="y-only" style="overflow:scroll;width:40px;height:40px">
      <div id="sticky-top" style="position:sticky;top:8px">t</div>
      <div style="height:200px"></div>
    </div>
    <div id="x-only" style="overflow:scroll;width:40px;height:40px">
      <div id="sticky-left" style="position:sticky;left:8px">l</div>
      <div style="width:200px;height:20px"></div>
    </div>
    <div id="both" style="overflow:auto;width:40px;height:40px">
      <div id="sticky-same" style="position:sticky;top:8px;left:12px">b</div>
      <div style="width:200px;height:200px"></div>
    </div>
    <div id="br" style="overflow:scroll;width:40px;height:40px">
      <div id="sticky-br" style="position:sticky;bottom:4px;right:6px">br</div>
      <div style="width:200px;height:200px"></div>
    </div>
    <div id="visible-scroll" style="overflow-x:visible;overflow-y:scroll;width:40px;height:40px">
      <div id="sticky-visible" style="position:sticky;top:1px;left:2px">v</div>
      <div style="width:200px;height:200px"></div>
    </div>
    <div id="wm" style="writing-mode:vertical-rl;overflow-inline:clip;overflow-block:scroll;width:40px;height:80px">
      <div id="sticky-wm" style="position:sticky;top:4px;left:4px">w</div>
      <div style="width:20px;height:200px"></div>
    </div>
    <div id="sticky-doc" style="position:sticky;top:0">doc</div>
  )HTML");
  // innerHTML does not parse declarative shadow roots.
  ShadowRoot& shadow = GetElementById("host")->AttachShadowRootForTesting(
      ShadowRootMode::kOpen);
  shadow.SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <div id="shadow-transform" style="transform:translateZ(0)">
      <div id="shadow-abs" style="position:absolute">s</div>
    </div>
  )HTML");
  To<HTMLDialogElement>(GetElementById("top-dialog"))
      ->showModal(ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhasesForTest();
  // srcdoc iframes load on a posted task.
  test::RunPendingTasks();
  UpdateAllLifecyclePhasesForTest();

  LayoutRelationshipSession session(this);

  auto expect = [&](const char* id) {
    Element* element = GetElementById(id);
    ExpectRelationshipsMatchBlink(session.Query(element), element,
                                  session.DomAgent());
  };

  expect("abs-child");
  expect("abs-transform-child");
  expect("abs-filter-child");
  expect("abs-perspective-child");
  expect("abs-contain-child");
  expect("abs-will-child");
  expect("abs-contents-child");
  expect("abs-icb");
  expect("fieldset-abs");
  expect("fixed-plain");
  expect("fixed-in-relative");
  expect("fixed-in-transform");
  expect("fixed-in-filter");
  expect("fixed-in-perspective");
  expect("top-dialog");
  expect("scroll-auto-child");
  expect("scroll-auto-empty-child");
  expect("scroll-hidden-child");
  expect("scroll-clip-child");
  expect("scroll-nested-child");
  expect("scroll-transformed-child");
  expect("in-flow");
  expect("sticky-split");
  expect("sticky-rev");
  expect("sticky-top");
  expect("sticky-left");
  expect("sticky-same");
  expect("sticky-br");
  expect("sticky-visible");
  expect("sticky-wm");
  expect("sticky-doc");

  Element* host = GetElementById("host");
  ASSERT_TRUE(host->GetShadowRoot());
  Element* shadow_abs =
      host->GetShadowRoot()->getElementById(AtomicString("shadow-abs"));
  ASSERT_TRUE(shadow_abs);
  ExpectRelationshipsMatchBlink(session.Query(shadow_abs), shadow_abs,
                                session.DomAgent());

  // The static wrapper is the layout parent and is not the containing block.
  Element* abs_child = GetElementById("abs-child");
  protocol::CSS::LayoutRelationships* abs_rel = session.Query(abs_child);
  EXPECT_NE(abs_rel->getContainingBlock(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("abs-static")));
  EXPECT_EQ(abs_rel->getContainingBlock(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("abs-relative")));
  Node* layout_parent = LayoutTreeBuilderTraversal::LayoutParent(*abs_child);
  EXPECT_EQ(session.DomAgent()->BoundNodeId(layout_parent),
            NodeId(session.DomAgent(), GetElementById("abs-static")));

  EXPECT_EQ(session.Query(GetElementById("abs-icb"))
                ->getContainingBlock(nullptr)
                ->getKind(),
            "initial-containing-block");
  EXPECT_TRUE(GetElementById("fieldset-abs")
                  ->GetLayoutObject()
                  ->ContainerForAbsolutePosition()
                  ->InclusiveContainingBlock()
                  ->IsAnonymous());
  EXPECT_EQ(session.Query(GetElementById("fieldset-abs"))
                ->getContainingBlock(nullptr)
                ->getKind(),
            "anonymous");

  protocol::CSS::LayoutRelationships* fixed_plain =
      session.Query(GetElementById("fixed-plain"));
  EXPECT_EQ(fixed_plain->getContainingBlock(nullptr)->getKind(),
            "initial-containing-block");
  EXPECT_EQ(fixed_plain->getScrollContainer(nullptr)->getKind(), "document");
  EXPECT_TRUE(
      fixed_plain->getScrollContainer(nullptr)->getFixedToViewport(false));

  protocol::CSS::LayoutRelationships* fixed_transform =
      session.Query(GetElementById("fixed-in-transform"));
  EXPECT_EQ(fixed_transform->getContainingBlock(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("fixed-transform")));
  EXPECT_FALSE(
      fixed_transform->getScrollContainer(nullptr)->getFixedToViewport(false));

  // Top layer is contained by the initial containing block. The flat-tree
  // layout parent remains the transformed DOM ancestor.
  protocol::CSS::LayoutRelationships* dialog =
      session.Query(GetElementById("top-dialog"));
  EXPECT_EQ(dialog->getContainingBlock(nullptr)->getKind(),
            "initial-containing-block");
  EXPECT_EQ(session.DomAgent()->BoundNodeId(LayoutTreeBuilderTraversal::LayoutParent(
                *GetElementById("top-dialog"))),
            NodeId(session.DomAgent(), GetElementById("dialog-transform")));

  EXPECT_TRUE(GetElementById("scroll-hidden")
                  ->GetLayoutObject()
                  ->IsScrollContainer());
  EXPECT_FALSE(
      GetElementById("scroll-clip")->GetLayoutObject()->IsScrollContainer());
  EXPECT_NE(session.Query(GetElementById("scroll-clip-child"))
                ->getScrollContainer(nullptr)
                ->getKind(),
            "element");
  EXPECT_TRUE(GetElementById("scroll-auto-empty")
                  ->GetLayoutObject()
                  ->IsScrollContainer());
  EXPECT_EQ(session.Query(GetElementById("scroll-nested-child"))
                ->getScrollContainer(nullptr)
                ->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("scroll-inner")));

  protocol::CSS::StickyScrollContainers* split =
      session.Query(GetElementById("sticky-split"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_EQ(split->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("inner-x")));
  EXPECT_EQ(split->getVertical(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("outer-y")));
  EXPECT_NE(split->getHorizontal(nullptr)->getNodeId(0),
            split->getVertical(nullptr)->getNodeId(0));

  protocol::CSS::StickyScrollContainers* reversed =
      session.Query(GetElementById("sticky-rev"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_EQ(reversed->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("outer-x")));
  EXPECT_EQ(reversed->getVertical(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("inner-y")));

  protocol::CSS::StickyScrollContainers* top_only =
      session.Query(GetElementById("sticky-top"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_FALSE(top_only->hasHorizontal());
  EXPECT_EQ(top_only->getVertical(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("y-only")));

  protocol::CSS::StickyScrollContainers* left_only =
      session.Query(GetElementById("sticky-left"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_FALSE(left_only->hasVertical());
  EXPECT_EQ(left_only->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("x-only")));

  protocol::CSS::LayoutRelationships* same =
      session.Query(GetElementById("sticky-same"));
  EXPECT_EQ(same->getStickyScrollContainers(nullptr)
                ->getHorizontal(nullptr)
                ->getNodeId(0),
            same->getStickyScrollContainers(nullptr)
                ->getVertical(nullptr)
                ->getNodeId(0));
  EXPECT_EQ(same->getScrollContainer(nullptr)->getNodeId(0),
            same->getStickyScrollContainers(nullptr)
                ->getVertical(nullptr)
                ->getNodeId(0));
  EXPECT_TRUE(same->hasScrollContainer());
  EXPECT_TRUE(same->hasStickyScrollContainers());

  protocol::CSS::StickyScrollContainers* bottom_right =
      session.Query(GetElementById("sticky-br"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_EQ(bottom_right->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("br")));
  EXPECT_EQ(bottom_right->getVertical(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("br")));

  Element* visible = GetElementById("visible-scroll");
  EXPECT_EQ(visible->GetLayoutObject()->StyleRef().OverflowX(),
            EOverflow::kAuto);
  EXPECT_EQ(visible->GetLayoutObject()->StyleRef().OverflowY(),
            EOverflow::kScroll);
  protocol::CSS::StickyScrollContainers* visible_sticky =
      session.Query(GetElementById("sticky-visible"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_EQ(visible_sticky->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), visible));
  EXPECT_EQ(visible_sticky->getVertical(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), visible));

  Element* wm = GetElementById("wm");
  EXPECT_EQ(wm->GetLayoutObject()->StyleRef().OverflowX(), EOverflow::kScroll);
  EXPECT_EQ(wm->GetLayoutObject()->StyleRef().OverflowY(), EOverflow::kClip);
  protocol::CSS::StickyScrollContainers* wm_sticky =
      session.Query(GetElementById("sticky-wm"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_EQ(wm_sticky->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), wm));
  EXPECT_EQ(wm_sticky->getVertical(nullptr)->getKind(), "document");

  EXPECT_EQ(session.Query(GetElementById("sticky-doc"))
                ->getStickyScrollContainers(nullptr)
                ->getVertical(nullptr)
                ->getKind(),
            "document");
  EXPECT_FALSE(session.Query(GetElementById("sticky-doc"))
                   ->getStickyScrollContainers(nullptr)
                   ->hasHorizontal());
}

TEST_F(InspectorCSSAgentTest, LayoutRelationshipsFollowLegacySingleAxis) {
  ScopedSingleAxisScrollContainersForTest single_axis(false);
  SetHtmlInnerHTML(R"HTML(
    <div id="outer-y" style="overflow-x:clip;overflow-y:scroll;width:80px;height:40px">
      <div id="inner-x" style="overflow-x:scroll;overflow-y:clip;width:70px;height:120px">
        <div id="sticky-split" style="position:sticky;top:8px;left:12px">s</div>
        <div style="width:300px;height:300px"></div>
      </div>
    </div>
    <div id="y-only" style="overflow:scroll;width:40px;height:40px">
      <div id="sticky-top" style="position:sticky;top:8px">t</div>
      <div style="height:200px"></div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  LayoutRelationshipSession session(this);

  Element* inner = GetElementById("inner-x");
  EXPECT_EQ(inner->GetLayoutObject()->StyleRef().OverflowY(),
            EOverflow::kHidden);
  ExpectRelationshipsMatchBlink(session.Query(GetElementById("sticky-split")),
                                GetElementById("sticky-split"),
                                session.DomAgent());
  protocol::CSS::StickyScrollContainers* split =
      session.Query(GetElementById("sticky-split"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_EQ(split->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), inner));
  EXPECT_EQ(split->getVertical(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), inner));

  ExpectRelationshipsMatchBlink(session.Query(GetElementById("sticky-top")),
                                GetElementById("sticky-top"),
                                session.DomAgent());
  protocol::CSS::StickyScrollContainers* top_only =
      session.Query(GetElementById("sticky-top"))
          ->getStickyScrollContainers(nullptr);
  EXPECT_TRUE(top_only->hasHorizontal());
  EXPECT_TRUE(top_only->hasVertical());
  EXPECT_EQ(top_only->getHorizontal(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("y-only")));
  EXPECT_EQ(top_only->getVertical(nullptr)->getNodeId(0),
            NodeId(session.DomAgent(), GetElementById("y-only")));
}

class InspectorCSSAgentSubframeTest : public PageTestBase {
 protected:
  void SetUp() override {
    // PageTestBase's default frame client does not create child frames.
    SetupPageWithClients(nullptr,
                         MakeGarbageCollected<SingleChildLocalFrameClient>());
  }
};

TEST_F(InspectorCSSAgentSubframeTest,
       LayoutRelationshipsFixedInsideTransformedFrame) {
  ScopedSingleAxisScrollContainersForTest single_axis(true);
  SetHtmlInnerHTML(R"HTML(<iframe id="frame"></iframe>)HTML");
  UpdateAllLifecyclePhasesForTest();
  test::RunPendingTasks();

  auto* frame = To<HTMLIFrameElement>(GetElementById("frame"));
  Document* child = frame->contentDocument();
  ASSERT_TRUE(child);
  ASSERT_TRUE(child->body());
  // srcdoc navigation stays asynchronous in this fixture. The child document
  // is the iframe's own layout tree, which is the relationship under test.
  child->body()->SetInnerHTMLWithoutTrustedTypes(R"HTML(
    <div id="frame-transform" style="transform:translateZ(0)">
      <div id="frame-fixed" style="position:fixed">f</div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  LayoutRelationshipSession session(this);
  Element* frame_fixed = child->getElementById(AtomicString("frame-fixed"));
  Element* frame_transform =
      child->getElementById(AtomicString("frame-transform"));
  ASSERT_TRUE(frame_fixed);
  ASSERT_TRUE(frame_transform);
  ExpectRelationshipsMatchBlink(session.Query(frame_fixed), frame_fixed,
                                session.DomAgent());
  EXPECT_EQ(session.Query(frame_fixed)
                ->getContainingBlock(nullptr)
                ->getNodeId(0),
            NodeId(session.DomAgent(), frame_transform));
  EXPECT_FALSE(session.Query(frame_fixed)
                   ->getScrollContainer(nullptr)
                   ->getFixedToViewport(false));
}

}  // namespace blink
