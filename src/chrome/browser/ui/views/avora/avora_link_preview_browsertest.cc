// Copyright 2026 Avora. All rights reserved.

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_marker.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_tab_state.h"
#include "chrome/browser/ui/views/avora/avora_link_preview_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/tabs/common/pinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/views/view_utils.h"

namespace avora {

namespace {

using SecondaryContentsMode = MultiContentsView::SecondaryContentsMode;

// A host page carrying one target="_blank" link, which is the gesture that
// opens the Window Overlay.
constexpr char kHostPage[] =
    "/avora_link_preview_host.html";

// A host page whose target="_blank" link goes to another origin, standing in
// for a logged-out site whose "Sign in" opens an identity provider.
constexpr char kSignInHostPage[] = "/avora_link_preview_signin_host.html";
constexpr char kAppHost[] = "app.test";
constexpr char kIdentityHost[] = "idp.test";

}  // namespace

// Covers the Window Overlay's tab lifecycle.  The overlay makes its preview
// Chromium's real active tab so that extension APIs and per-tab browser UI
// resolve to the page the user is looking at; these tests pin down the
// transitions into and out of that state, which BrowserView::
// OnActiveTabChanged() is sensitive to the ordering of.
class AvoraLinkPreviewBrowserTest : public InProcessBrowserTest {
 public:
  AvoraLinkPreviewBrowserTest() = default;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &AvoraLinkPreviewBrowserTest::HandleRequest, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL(kHostPage)));
  }

 protected:
  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("text/html");
    if (request.relative_url == kHostPage) {
      // |cookieAtLoad| is captured once, at load time, so its value is proof
      // of whether the tab was reloaded rather than of the current jar.
      response->set_content(
          "<!doctype html><title>host</title>"
          "<a id='go' href='/target.html' target='_blank'>open</a>"
          "<script>window.cookieAtLoad = document.cookie;</script>");
    } else if (request.relative_url == kSignInHostPage) {
      response->set_content(
          "<!doctype html><title>signed out</title><a id='go' href='" +
          embedded_test_server()->GetURL(kIdentityHost, "/signin.html").spec() +
          "' target='_blank'>sign in</a>");
    } else {
      // Writes a cookie the host would only pick up after a reload, so tests
      // can tell whether dismissing the overlay navigated the tab underneath.
      response->set_content(
          "<!doctype html><title>target</title>"
          "<script>document.cookie = 'preview_ran=1; path=/';</script>");
    }
    return response;
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  MultiContentsView* multi_contents_view() {
    return browser_view()->multi_contents_view();
  }

  TabStripModel* model() { return browser()->tab_strip_model(); }

  content::WebContents* host_tab() { return host_tab_; }

  // Clicks the target="_blank" link, which BrowserWebContentsDelegate::
  // AddNewContents() intercepts into the overlay.  Returns the preview.
  content::WebContents* OpenOverlay() {
    host_tab_ = model()->GetActiveWebContents();
    const int tabs_before = model()->count();
    EXPECT_TRUE(content::ExecJs(host_tab_, "document.getElementById('go')."
                                           "click();"));
    EXPECT_TRUE(base::test::RunUntil(
        [&]() { return model()->count() == tabs_before + 1; }));
    content::WebContents* preview = GetLinkPreviewContents(host_tab_);
    EXPECT_TRUE(preview);
    return preview;
  }

  // BrowserView exposes the region view; the TabStripView that actually owns
  // the rows is a descendant of it.
  static TabStripView* FindTabStripView(views::View* root) {
    if (!root) {
      return nullptr;
    }
    if (TabStripView* strip = views::AsViewClass<TabStripView>(root)) {
      return strip;
    }
    for (views::View* child : root->children()) {
      if (TabStripView* strip = FindTabStripView(child)) {
        return strip;
      }
    }
    return nullptr;
  }

  // The TabView drawn for |contents|, or null when the strip has no row for
  // it at all.
  TabView* FindTabView(content::WebContents* contents) {
    TabStripView* strip = FindTabStripView(browser_view()->tab_strip_view());
    if (!strip) {
      return nullptr;
    }
    views::View* const containers[] = {
        strip->GetUnpinnedTabsContainer(),
        strip->GetPinnedTabsContainer(),
    };
    for (views::View* container : containers) {
      if (!container) {
        continue;
      }
      for (views::View* child : container->children()) {
        TabView* tab_view = views::AsViewClass<TabView>(child);
        if (!tab_view || !tab_view->GetTabInterface()) {
          continue;
        }
        if (tab_view->GetTabInterface()->GetContents() == contents) {
          return tab_view;
        }
      }
    }
    return nullptr;
  }

  // What chrome.tabs.query({active:true}) would return for |contents|:
  // TabsQueryFunction filters on TabInterface::IsActivated(), and
  // ExtensionTabUtil copies the same value into Tab.active.
  bool IsActiveToExtensions(content::WebContents* contents) {
    tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
    return tab && tab->IsActivated();
  }

  content::WebContents* PrimaryContainerContents() {
    return multi_contents_view()
        ->GetPrimaryContentsContainerView()
        ->contents_view()
        ->web_contents();
  }

  content::WebContents* SecondaryContainerContents() {
    return multi_contents_view()
        ->contents_container_views()[1]
        ->contents_view()
        ->web_contents();
  }

 private:
  raw_ptr<content::WebContents> host_tab_ = nullptr;
};

// host -> open overlay -> preview becomes the active tab.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, OpenOverlayActivatesPreview) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);

  EXPECT_EQ(preview, model()->GetActiveWebContents());
  EXPECT_TRUE(IsLinkPreviewTab(preview));
  EXPECT_EQ(SecondaryContentsMode::kOverlay,
            multi_contents_view()->secondary_contents_mode());
  EXPECT_EQ(preview, SecondaryContainerContents());
}

// chrome.tabs.query({active:true}) returns the preview while the overlay is
// open -- the property the whole architecture exists for.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest,
                       PreviewIsActiveToExtensionsWhileOpen) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);

  EXPECT_TRUE(IsActiveToExtensions(preview));
  EXPECT_FALSE(IsActiveToExtensions(host_tab()));
}

// The preview is not drawn in Avora's visible tab strip.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest,
                       PreviewHiddenFromVisibleTabStrip) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);

  TabView* const preview_row = FindTabView(preview);
  // Either there is no row at all, or the row exists but is hidden.  Both are
  // acceptable; what must not happen is a visible row for the preview.
  EXPECT_TRUE(!preview_row || !preview_row->GetVisible());

  TabView* const host_row = FindTabView(host_tab());
  ASSERT_TRUE(host_row);
  EXPECT_TRUE(host_row->GetVisible());
}

// The host stays live and rendered in the primary container behind the card.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest,
                       HostRemainsVisibleBehindOverlay) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);

  EXPECT_EQ(host_tab(), PrimaryContainerContents());
  EXPECT_EQ(content::Visibility::VISIBLE, host_tab()->GetVisibility());
  EXPECT_EQ(content::Visibility::VISIBLE, preview->GetVisibility());
}

// Dismissing the overlay hands activation back to the host and returns the
// secondary container.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, DismissRestoresHost) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);
  content::WebContents* const host = host_tab();

  browser_view()->HideAvoraLinkPreview();
  // Activation is handed back on a fresh stack -- see
  // BrowserView::ActivateAvoraTabSoon() -- so wait rather than asserting
  // synchronously.
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return model()->GetActiveWebContents() == host; }));

  EXPECT_EQ(SecondaryContentsMode::kNone,
            multi_contents_view()->secondary_contents_mode());
  EXPECT_EQ(host, PrimaryContainerContents());
  EXPECT_EQ(nullptr, SecondaryContainerContents());
}

// Switching straight from the overlay to a third tab.  This is the ordering
// the spike got wrong: the generic contents swap must not run while the card
// is still the active container, or the host is evicted from the primary
// container and the incoming tab is mounted into the card.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest,
                       SwitchDirectlyFromOverlayToAnotherTab) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);

  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/third.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));

  content::WebContents* const third = model()->GetActiveWebContents();
  ASSERT_TRUE(third);
  EXPECT_NE(preview, third);
  EXPECT_EQ(SecondaryContentsMode::kNone,
            multi_contents_view()->secondary_contents_mode());
  // The incoming tab belongs in the full-bleed container, not the card.
  EXPECT_EQ(third, PrimaryContainerContents());
  EXPECT_EQ(nullptr, SecondaryContainerContents());
  EXPECT_EQ(content::Visibility::VISIBLE, third->GetVisibility());
}

// Closing the preview while it is the active tab.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, CloseOverlayWhileActive) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);
  content::WebContents* const host = host_tab();

  const int preview_index = model()->GetIndexOfWebContents(preview);
  ASSERT_NE(TabStripModel::kNoTab, preview_index);
  model()->CloseWebContentsAt(preview_index, TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !GetLinkPreviewContents(host) &&
           model()->GetActiveWebContents() == host;
  }));

  EXPECT_EQ(SecondaryContentsMode::kNone,
            multi_contents_view()->secondary_contents_mode());
  EXPECT_EQ(host, PrimaryContainerContents());
}

// Closing the host while its overlay is open.  The preview is torn down with
// it and the selection must land on a real tab.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, CloseHostWithOverlayOpen) {
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL(kHostPage),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));

  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);
  content::WebContents* const host = host_tab();

  const int host_index = model()->GetIndexOfWebContents(host);
  ASSERT_NE(TabStripModel::kNoTab, host_index);
  model()->CloseWebContentsAt(host_index, TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(base::test::RunUntil([&]() { return model()->count() == 1; }));

  EXPECT_EQ(SecondaryContentsMode::kNone,
            multi_contents_view()->secondary_contents_mode());
  content::WebContents* const active = model()->GetActiveWebContents();
  ASSERT_TRUE(active);
  EXPECT_FALSE(IsLinkPreviewTab(active));
  EXPECT_EQ(active, PrimaryContainerContents());
}

// Opening and dismissing repeatedly must leave no residue: no stray tabs, and
// the containers back in their single-contents arrangement every time.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, RepeatedOpenDismissCycles) {
  const int tabs_at_start = model()->count();

  for (int i = 0; i < 5; ++i) {
    content::WebContents* const preview = OpenOverlay();
    ASSERT_TRUE(preview) << "cycle " << i;
    ASSERT_EQ(preview, model()->GetActiveWebContents()) << "cycle " << i;

    content::WebContents* const host = host_tab();
    browser_view()->HideAvoraLinkPreview();
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return model()->count() == tabs_at_start &&
                       model()->GetActiveWebContents() == host; }))
        << "cycle " << i;

    EXPECT_EQ(SecondaryContentsMode::kNone,
              multi_contents_view()->secondary_contents_mode())
        << "cycle " << i;
    EXPECT_EQ(nullptr, SecondaryContainerContents()) << "cycle " << i;
  }
}

// The Space's remembered tab must stay the host: a preview is a glance, and
// restoring a Space to a preview that has since been closed would strand it.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest,
                       PreviewDoesNotBecomeSpaceRememberedTab) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);
  content::WebContents* const host = host_tab();

  // Leaving and returning to the Space restores its remembered tab; that must
  // be the host even though the preview was active when the Space was left.
  browser_view()->HideAvoraLinkPreview();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !GetLinkPreviewContents(host) &&
           model()->GetActiveWebContents() == host;
  }));
}

// Closing the overlay must leave the host exactly where it was.  Reloading
// would throw away in-page state (scroll, form fields, SPA view) for a
// gesture that is only meant to put the card away.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, DismissDoesNotReloadHost) {
  ASSERT_TRUE(OpenOverlay());
  content::WebContents* const host = host_tab();
  ASSERT_EQ("", content::EvalJs(host, "window.cookieAtLoad").ExtractString());

  browser_view()->HideAvoraLinkPreview();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return model()->GetActiveWebContents() == host; }));

  EXPECT_EQ("", content::EvalJs(host, "window.cookieAtLoad").ExtractString());
  EXPECT_FALSE(host->IsLoading());
}

// Same for a preview that closes itself (window.close()).  That path never
// reaches HideAvoraLinkPreview(): OnActiveTabChanged() detaches the overlay
// first.  The host still must not reload.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest,
                       PreviewClosingItselfDoesNotReloadHost) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);
  content::WebContents* const host = host_tab();
  ASSERT_EQ("", content::EvalJs(host, "window.cookieAtLoad").ExtractString());

  ASSERT_TRUE(content::ExecJs(preview, "window.close();"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !GetLinkPreviewContents(host) &&
           model()->GetActiveWebContents() == host;
  }));

  EXPECT_EQ("", content::EvalJs(host, "window.cookieAtLoad").ExtractString());
  EXPECT_FALSE(host->IsLoading());
}

// A sign-in that opens in the card and lands back on the host's site belongs
// to the host: the overlay goes away and the host loads the signed-in page,
// rather than the host staying signed out while the card holds the session.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, SignInReturnLoadsInHost) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(kAppHost, kSignInHostPage)));
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);
  content::WebContents* const host = host_tab();
  ASSERT_TRUE(content::WaitForLoadStop(preview));
  // Opening onto the identity provider is an ordinary preview.
  ASSERT_EQ(preview, GetLinkPreviewContents(host));

  const GURL landed = embedded_test_server()->GetURL(kAppHost, "/inbox.html");
  ASSERT_TRUE(content::ExecJs(
      preview, content::JsReplace("location.href = $1;", landed)));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !GetLinkPreviewContents(host) &&
           model()->GetActiveWebContents() == host;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(host));

  EXPECT_EQ(landed, host->GetLastCommittedURL());
  EXPECT_EQ(SecondaryContentsMode::kNone,
            multi_contents_view()->secondary_contents_mode());
}

// A preview that opened on the host's own site is just a link, however far
// the user browses inside it, and must stay in the card.
IN_PROC_BROWSER_TEST_F(AvoraLinkPreviewBrowserTest, SameSitePreviewStaysInCard) {
  content::WebContents* const preview = OpenOverlay();
  ASSERT_TRUE(preview);
  ASSERT_TRUE(content::WaitForLoadStop(preview));

  ASSERT_TRUE(content::NavigateToURL(
      preview, embedded_test_server()->GetURL("/another.html")));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(preview, GetLinkPreviewContents(host_tab()));
  EXPECT_EQ(preview, model()->GetActiveWebContents());
}

}  // namespace avora
