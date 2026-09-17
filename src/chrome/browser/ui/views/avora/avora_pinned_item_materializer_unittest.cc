// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_pinned_item_materializer.h"

#include <memory>

#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_tab_space.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/test_tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/avora/avora_pinned_item_tab_marker.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace avora {
namespace {

// Exercises FindMaterializedPinnedItemTab() against real TabStripModel
// instances -- two of them, standing in for two independent Avora windows --
// so the window-isolation guarantee is verified against the actual model
// class, not a hand-rolled stand-in.
class AvoraPinnedItemMaterializerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    profile_ = std::make_unique<TestingProfile>();
    window_a_ =
        std::make_unique<TabStripModel>(&delegate_a_, profile_.get());
    window_b_ =
        std::make_unique<TabStripModel>(&delegate_b_, profile_.get());

    // A standalone PrefService, deliberately separate from the
    // TestingProfile's own (which has no Avora prefs registered and cannot
    // be registered post-construction) -- PinnedItemsManager only needs a
    // PrefService, not specifically the profile's.
    SpaceManager::RegisterProfilePrefs(pinned_prefs_.registry());
    SidebarItemStore::RegisterProfilePrefs(pinned_prefs_.registry());
    PinnedItemsManager::RegisterProfilePrefs(pinned_prefs_.registry());
    pinned_items_manager_ =
        std::make_unique<PinnedItemsManager>(&pinned_prefs_);
  }

  void TearDown() override {
    window_a_->CloseAllTabs();
    window_b_->CloseAllTabs();
  }

  std::unique_ptr<content::WebContents> CreateWebContents() {
    return content::WebContentsTester::CreateTestWebContents(profile_.get(),
                                                              nullptr);
  }

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler rvh_test_enabler_;
  // Skips TabFeatures::Init()'s CHECK(tab.GetBrowserWindowInterface()) --
  // this test constructs bare TabStripModel instances with no owning
  // BrowserWindowInterface, which is fine for exercising
  // FindMaterializedPinnedItemTab() but would otherwise crash on
  // AppendWebContents().
  const tabs::TabModel::PreventFeatureInitializationForTesting
      prevent_feature_init_;
  std::unique_ptr<TestingProfile> profile_;
  TestTabStripModelDelegate delegate_a_;
  TestTabStripModelDelegate delegate_b_;
  std::unique_ptr<TabStripModel> window_a_;
  std::unique_ptr<TabStripModel> window_b_;

  TestingPrefServiceSimple pinned_prefs_;
  std::unique_ptr<PinnedItemsManager> pinned_items_manager_;
};

// ── Single-window resolution ─────────────────────────────────────────────────

TEST_F(AvoraPinnedItemMaterializerTest, ReturnsNoTabWhenNothingMarked) {
  window_a_->AppendWebContents(CreateWebContents(), true);

  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-1"),
      TabStripModel::kNoTab);
}

TEST_F(AvoraPinnedItemMaterializerTest, FindsMarkedTabInSameSpace) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  MarkPinnedItemTab(raw, "item-1");
  SetTabSpaceId(raw, "space-1");

  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-1"), 0);
}

TEST_F(AvoraPinnedItemMaterializerTest, DoesNotMatchDifferentItemId) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  MarkPinnedItemTab(raw, "item-1");
  SetTabSpaceId(raw, "space-1");

  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-2", "space-1"),
      TabStripModel::kNoTab);
}

TEST_F(AvoraPinnedItemMaterializerTest, RespectsSpaceScoping) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  MarkPinnedItemTab(raw, "item-1");
  SetTabSpaceId(raw, "space-1");

  // Marked and materialized for space-1; a lookup for space-2 must not
  // resolve to it even though the item id matches.
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-2"),
      TabStripModel::kNoTab);
}

TEST_F(AvoraPinnedItemMaterializerTest, UntaggedSpaceMatchesAnySpace) {
  // Mirrors TabBelongsToSpace()'s own contract: a tab never tagged with a
  // Space (SetTabSpaceId never called) is treated as belonging to every
  // Space, so it must still resolve regardless of which Space is asked
  // about.
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  MarkPinnedItemTab(raw, "item-1");

  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-1"), 0);
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-2"), 0);
}

TEST_F(AvoraPinnedItemMaterializerTest, EmptyItemIdNeverMatches) {
  auto contents = CreateWebContents();
  window_a_->AppendWebContents(std::move(contents), true);

  EXPECT_EQ(FindMaterializedPinnedItemTab(window_a_.get(), "", "space-1"),
           TabStripModel::kNoTab);
}

TEST_F(AvoraPinnedItemMaterializerTest, NullTabStripReturnsNoTab) {
  EXPECT_EQ(FindMaterializedPinnedItemTab(nullptr, "item-1", "space-1"),
           TabStripModel::kNoTab);
}

// ── Multi-window isolation ───────────────────────────────────────────────────
//
// These are the tests the Phase 2 requirements specifically called for: the
// same pinned item, same Space, but two independent windows (independent
// TabStripModel instances here).

TEST_F(AvoraPinnedItemMaterializerTest,
      MaterializedInWindowADoesNotLeakToWindowB) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  MarkPinnedItemTab(raw, "item-1");
  SetTabSpaceId(raw, "space-1");

  // Same item id, same Space id -- window B simply has no tabs at all, so a
  // lookup against its model must find nothing, never window A's tab.
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-1"), 0);
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_b_.get(), "item-1", "space-1"),
      TabStripModel::kNoTab);
}

TEST_F(AvoraPinnedItemMaterializerTest,
      IndependentMaterializationInTwoWindowsForSameItem) {
  // The same pinned item materialized independently in both windows, as
  // happens when the user clicks it in window A, then again in window B.
  auto contents_a = CreateWebContents();
  content::WebContents* raw_a = contents_a.get();
  window_a_->AppendWebContents(std::move(contents_a), true);
  MarkPinnedItemTab(raw_a, "item-1");
  SetTabSpaceId(raw_a, "space-1");

  auto contents_b = CreateWebContents();
  content::WebContents* raw_b = contents_b.get();
  window_b_->AppendWebContents(std::move(contents_b), true);
  MarkPinnedItemTab(raw_b, "item-1");
  SetTabSpaceId(raw_b, "space-1");

  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-1"), 0);
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_b_.get(), "item-1", "space-1"), 0);

  // Closing window A's materialized tab must not affect window B's
  // independent one.
  window_a_->CloseAllTabs();
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-1"),
      TabStripModel::kNoTab);
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_b_.get(), "item-1", "space-1"), 0);
}

TEST_F(AvoraPinnedItemMaterializerTest,
      SecondUnrelatedTabInOtherWindowDoesNotInterfere) {
  auto contents_a = CreateWebContents();
  content::WebContents* raw_a = contents_a.get();
  window_a_->AppendWebContents(std::move(contents_a), true);
  MarkPinnedItemTab(raw_a, "item-1");
  SetTabSpaceId(raw_a, "space-1");

  // Window B has an entirely unrelated tab (no marker at all).
  window_b_->AppendWebContents(CreateWebContents(), true);

  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_a_.get(), "item-1", "space-1"), 0);
  EXPECT_EQ(
      FindMaterializedPinnedItemTab(window_b_.get(), "item-1", "space-1"),
      TabStripModel::kNoTab);
}

// ── PinAndCreatePinnedItem / UnpinAndRemovePinnedItem ───────────────────────
//
// These exercise the Phase 2.5 "native pin gesture creates real persistence"
// requirement directly against a real TabStripModel and a real
// PinnedItemsManager, rather than the view/drag-handler layer that calls
// them (which needs a full Widget/BrowserWindowInterface harness out of
// scope for a unit test).

using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST_F(AvoraPinnedItemMaterializerTest, PinCreatesPersistenceAndMarksTab) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  content::WebContentsTester::For(raw)->NavigateAndCommit(
      GURL("https://example.com/"));
  window_a_->AppendWebContents(std::move(contents), true);
  const int index = window_a_->GetIndexOfWebContents(raw);

  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_TRUE(window_a_->IsTabPinned(index));
  EXPECT_TRUE(IsPinnedItemTab(raw));
  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), SizeIs(1));
  EXPECT_EQ(pinned_items_manager_->GetPinnedItems()[0].id,
           GetPinnedItemIdForTab(raw));
}

TEST_F(AvoraPinnedItemMaterializerTest,
      PinReusesExistingItemForSameUrlNoDuplicate) {
  // Simulates: a pinned item for this URL already exists (e.g. from another
  // tab, or created by clicking the persisted row), and the user now also
  // native-pins a second, unrelated tab that happens to share the URL.
  const std::string item_id =
      pinned_items_manager_->AddPinnedItem("https://example.com/", "Example");
  ASSERT_FALSE(item_id.empty());

  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  content::WebContentsTester::For(raw)->NavigateAndCommit(
      GURL("https://example.com/"));
  window_a_->AppendWebContents(std::move(contents), true);

  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), SizeIs(1));
  EXPECT_EQ(GetPinnedItemIdForTab(raw), item_id);
}

TEST_F(AvoraPinnedItemMaterializerTest,
      PinningAlreadyMaterializedTabDoesNotDuplicateRecord) {
  // A tab already materialized from a click (Phase 2) -- not natively
  // pinned yet -- gets native-pinned via drag. Must not create a second
  // persisted item for the one it already backs.
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  const std::string item_id =
      pinned_items_manager_->AddPinnedItem("https://example.com/", "Example");
  MarkPinnedItemTab(raw, item_id);

  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), SizeIs(1));
  EXPECT_EQ(GetPinnedItemIdForTab(raw), item_id);
  EXPECT_TRUE(window_a_->IsTabPinned(window_a_->GetIndexOfWebContents(raw)));
}

TEST_F(AvoraPinnedItemMaterializerTest, PinIsIdempotent) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  content::WebContentsTester::For(raw)->NavigateAndCommit(
      GURL("https://example.com/"));
  window_a_->AppendWebContents(std::move(contents), true);

  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);
  const std::string first_id = GetPinnedItemIdForTab(raw);
  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), SizeIs(1));
  EXPECT_EQ(GetPinnedItemIdForTab(raw), first_id);
}

TEST_F(AvoraPinnedItemMaterializerTest,
      UnpinRemovesPersistenceAndMarkerButKeepsTabOpen) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  content::WebContentsTester::For(raw)->NavigateAndCommit(
      GURL("https://example.com/"));
  window_a_->AppendWebContents(std::move(contents), true);
  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);
  const int index = window_a_->GetIndexOfWebContents(raw);
  ASSERT_TRUE(window_a_->IsTabPinned(index));

  UnpinAndRemovePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_FALSE(window_a_->IsTabPinned(window_a_->GetIndexOfWebContents(raw)));
  EXPECT_FALSE(IsPinnedItemTab(raw));
  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), IsEmpty());
  // The live page stays open as a normal tab -- unpinning must never close
  // it.
  EXPECT_NE(window_a_->GetIndexOfWebContents(raw), TabStripModel::kNoTab);
}

TEST_F(AvoraPinnedItemMaterializerTest,
      UnpinWithOnlyMarkerNoNativePinStillRemovesPersistence) {
  // A materialized-but-never-natively-pinned tab (Phase 2's click-to-
  // materialize path) unpinned via the same "Unpin Tab" action.
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  const std::string item_id =
      pinned_items_manager_->AddPinnedItem("https://example.com/", "Example");
  MarkPinnedItemTab(raw, item_id);
  ASSERT_FALSE(window_a_->IsTabPinned(window_a_->GetIndexOfWebContents(raw)));

  UnpinAndRemovePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_FALSE(IsPinnedItemTab(raw));
  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), IsEmpty());
  EXPECT_NE(window_a_->GetIndexOfWebContents(raw), TabStripModel::kNoTab);
}

TEST_F(AvoraPinnedItemMaterializerTest,
      UnpinWithOnlyNativePinNoMarkerJustUnpins) {
  // A tab natively pinned before this feature existed and never backfilled
  // -- no kPinned item exists for it at all. Must not crash.
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);
  const int index = window_a_->GetIndexOfWebContents(raw);
  window_a_->SetTabPinned(index, true);

  UnpinAndRemovePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_FALSE(window_a_->IsTabPinned(window_a_->GetIndexOfWebContents(raw)));
  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), IsEmpty());
}

TEST_F(AvoraPinnedItemMaterializerTest, UnpinOfUntouchedTabIsNoOp) {
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  window_a_->AppendWebContents(std::move(contents), true);

  UnpinAndRemovePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_NE(window_a_->GetIndexOfWebContents(raw), TabStripModel::kNoTab);
  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), IsEmpty());
}

TEST_F(AvoraPinnedItemMaterializerTest, RepinAfterUnpinCreatesFreshRecord) {
  // pin -> unpin -> pin again must never leave a duplicate or stale record
  // behind, and the second pin gets a new item (the old one was genuinely
  // removed by the unpin, not just hidden).
  auto contents = CreateWebContents();
  content::WebContents* raw = contents.get();
  content::WebContentsTester::For(raw)->NavigateAndCommit(
      GURL("https://example.com/"));
  window_a_->AppendWebContents(std::move(contents), true);

  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);
  const std::string first_id = GetPinnedItemIdForTab(raw);
  UnpinAndRemovePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);
  ASSERT_THAT(pinned_items_manager_->GetPinnedItems(), IsEmpty());

  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw);

  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), SizeIs(1));
  EXPECT_NE(GetPinnedItemIdForTab(raw), first_id);
}

TEST_F(AvoraPinnedItemMaterializerTest,
      PinInWindowADoesNotAffectWindowBsPersistentState) {
  // Pin/unpin in one window updates the SHARED persistent PinnedItemsManager
  // state -- that part is intentionally not window-scoped, since the
  // persisted item belongs to the Space, not to a window -- but must never
  // touch window B's live TabStripModel.
  auto contents_a = CreateWebContents();
  content::WebContents* raw_a = contents_a.get();
  content::WebContentsTester::For(raw_a)->NavigateAndCommit(
      GURL("https://example.com/"));
  window_a_->AppendWebContents(std::move(contents_a), true);

  window_b_->AppendWebContents(CreateWebContents(), true);
  const int window_b_count_before = window_b_->count();

  PinAndCreatePinnedItem(window_a_.get(), pinned_items_manager_.get(), raw_a);

  EXPECT_THAT(pinned_items_manager_->GetPinnedItems(), SizeIs(1));
  EXPECT_EQ(window_b_->count(), window_b_count_before);
  EXPECT_FALSE(window_b_->GetWebContentsAt(0) &&
              IsPinnedItemTab(window_b_->GetWebContentsAt(0)));
}

}  // namespace
}  // namespace avora
