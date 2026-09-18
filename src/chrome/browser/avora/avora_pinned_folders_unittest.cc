// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_pinned_folders.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/values.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Observer that counts notifications.
class TestObserver : public PinnedFoldersManager::Observer {
 public:
  void OnPinnedFoldersChanged() override { ++change_count_; }
  int change_count() const { return change_count_; }
  void Reset() { change_count_ = 0; }

 private:
  int change_count_ = 0;
};

class PinnedFoldersManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SpaceManager::RegisterProfilePrefs(prefs_.registry());
    SidebarItemStore::RegisterProfilePrefs(prefs_.registry());
    PinnedItemsManager::RegisterProfilePrefs(prefs_.registry());
    PinnedFoldersManager::RegisterProfilePrefs(prefs_.registry());

    items_ = std::make_unique<PinnedItemsManager>(&prefs_);
    manager_ = std::make_unique<PinnedFoldersManager>(&prefs_);
    manager_->AddObserver(&observer_);
  }

  void TearDown() override { manager_->RemoveObserver(&observer_); }

  std::unique_ptr<PinnedFoldersManager> CreateSecondManager() {
    return std::make_unique<PinnedFoldersManager>(&prefs_);
  }

  // Simulate an application restart by destroying and recreating both
  // managers over the same PrefService.
  void SimulateRestart() {
    manager_->RemoveObserver(&observer_);
    manager_.reset();
    items_.reset();
    items_ = std::make_unique<PinnedItemsManager>(&prefs_);
    manager_ = std::make_unique<PinnedFoldersManager>(&prefs_);
    manager_->AddObserver(&observer_);
  }

  // Returns the ids of |folder_id|'s members, or empty if the folder is
  // missing.
  std::vector<std::string> MembersOf(const std::string& folder_id) {
    for (const auto& folder : manager_->GetFolders()) {
      if (folder.id == folder_id) {
        return folder.ordered_item_ids;
      }
    }
    return {};
  }

  TestingPrefServiceSimple prefs_;
  std::unique_ptr<PinnedItemsManager> items_;
  std::unique_ptr<PinnedFoldersManager> manager_;
  TestObserver observer_;
};

// ── Create / persistence ─────────────────────────────────────────────────────

TEST_F(PinnedFoldersManagerTest, EmptyManagerReturnsNothing) {
  EXPECT_THAT(manager_->GetFolders(), IsEmpty());
}

TEST_F(PinnedFoldersManagerTest, AddFolderReturnsStableId) {
  const std::string id = manager_->AddFolder("Development");
  EXPECT_FALSE(id.empty());

  const auto folders = manager_->GetFolders();
  ASSERT_THAT(folders, SizeIs(1));
  EXPECT_EQ(folders[0].id, id);
  EXPECT_EQ(folders[0].name, "Development");
  EXPECT_TRUE(folders[0].expanded);
  EXPECT_THAT(folders[0].ordered_item_ids, IsEmpty());
}

TEST_F(PinnedFoldersManagerTest, EmptyFolderSurvivesRestart) {
  const std::string id = manager_->AddFolder("Development");
  SimulateRestart();

  const auto folders = manager_->GetFolders();
  ASSERT_THAT(folders, SizeIs(1));
  EXPECT_EQ(folders[0].id, id);
  EXPECT_EQ(folders[0].name, "Development");
  EXPECT_THAT(folders[0].ordered_item_ids, IsEmpty());
}

// ── Membership ───────────────────────────────────────────────────────────────

TEST_F(PinnedFoldersManagerTest, MoveItemToFolderAddsMembership) {
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://github.com", "GitHub");

  manager_->MoveItemToFolder(item_id, folder_id);

  EXPECT_THAT(MembersOf(folder_id), ElementsAre(item_id));
  EXPECT_EQ(manager_->GetFolderIdForItem(item_id), folder_id);
}

TEST_F(PinnedFoldersManagerTest, MembershipSurvivesRestartEvenWithNoLiveTab) {
  // The whole point of Phase 3: a folder and its membership exist
  // independently of any live WebContents -- nothing here ever touches a
  // TabStripModel.
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://github.com", "GitHub");
  manager_->MoveItemToFolder(item_id, folder_id);

  SimulateRestart();

  EXPECT_THAT(MembersOf(folder_id), ElementsAre(item_id));
  EXPECT_EQ(manager_->GetFolderIdForItem(item_id), folder_id);
}

TEST_F(PinnedFoldersManagerTest, RemoveItemFromFolderViaTopLevelMove) {
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://github.com", "GitHub");
  manager_->MoveItemToFolder(item_id, folder_id);

  manager_->MoveItemToFolder(item_id, std::string());  // Back to top-level.

  EXPECT_THAT(MembersOf(folder_id), IsEmpty());
  EXPECT_EQ(manager_->GetFolderIdForItem(item_id), "");
}

TEST_F(PinnedFoldersManagerTest, MoveBetweenFoldersRemovesFromSource) {
  const std::string folder_a = manager_->AddFolder("Frontend");
  const std::string folder_b = manager_->AddFolder("Backend");
  const std::string item_id = items_->AddPinnedItem("https://github.com", "GitHub");
  manager_->MoveItemToFolder(item_id, folder_a);

  manager_->MoveItemToFolder(item_id, folder_b);

  EXPECT_THAT(MembersOf(folder_a), IsEmpty());
  EXPECT_THAT(MembersOf(folder_b), ElementsAre(item_id));
  EXPECT_EQ(manager_->GetFolderIdForItem(item_id), folder_b);
}

TEST_F(PinnedFoldersManagerTest, MoveToSameFolderIsNoOpDoesNotReorder) {
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_a = items_->AddPinnedItem("https://a.com", "A");
  const std::string item_b = items_->AddPinnedItem("https://b.com", "B");
  manager_->MoveItemToFolder(item_a, folder_id);
  manager_->MoveItemToFolder(item_b, folder_id);

  observer_.Reset();
  manager_->MoveItemToFolder(item_a, folder_id);  // Already there.

  EXPECT_EQ(observer_.change_count(), 0);
  EXPECT_THAT(MembersOf(folder_id), ElementsAre(item_a, item_b));
}

TEST_F(PinnedFoldersManagerTest, ReorderWithinFolder) {
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_a = items_->AddPinnedItem("https://a.com", "A");
  const std::string item_b = items_->AddPinnedItem("https://b.com", "B");
  const std::string item_c = items_->AddPinnedItem("https://c.com", "C");
  manager_->MoveItemToFolder(item_a, folder_id);
  manager_->MoveItemToFolder(item_b, folder_id);
  manager_->MoveItemToFolder(item_c, folder_id);
  ASSERT_THAT(MembersOf(folder_id), ElementsAre(item_a, item_b, item_c));

  manager_->ReorderItemInFolder(folder_id, item_a, 2);

  EXPECT_THAT(MembersOf(folder_id), ElementsAre(item_b, item_c, item_a));
}

TEST_F(PinnedFoldersManagerTest, ReorderItemNotInFolderIsNoOp) {
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://a.com", "A");
  // item_id was never moved into folder_id.

  observer_.Reset();
  manager_->ReorderItemInFolder(folder_id, item_id, 0);

  EXPECT_EQ(observer_.change_count(), 0);
  EXPECT_THAT(MembersOf(folder_id), IsEmpty());
}

// ── Unpin / dangling ids ─────────────────────────────────────────────────────

TEST_F(PinnedFoldersManagerTest, RemoveItemFromAllFoldersLeavesNoDanglingId) {
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://github.com", "GitHub");
  manager_->MoveItemToFolder(item_id, folder_id);
  items_->RemovePinnedItem(item_id);  // Simulates the "unpin" half.

  manager_->RemoveItemFromAllFolders(item_id);

  EXPECT_THAT(MembersOf(folder_id), IsEmpty());
  EXPECT_EQ(manager_->GetFolderIdForItem(item_id), "");
}

TEST_F(PinnedFoldersManagerTest, RemoveItemFromAllFoldersWhenNotAMemberIsNoOp) {
  const std::string folder_id = manager_->AddFolder("Development");
  observer_.Reset();

  manager_->RemoveItemFromAllFolders("nonexistent-item");

  EXPECT_EQ(observer_.change_count(), 0);
  EXPECT_THAT(MembersOf(folder_id), IsEmpty());
}

// ── Delete folder ────────────────────────────────────────────────────────────

TEST_F(PinnedFoldersManagerTest, DeleteFolderPromotesChildrenToTopLevel) {
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_a = items_->AddPinnedItem("https://a.com", "A");
  const std::string item_b = items_->AddPinnedItem("https://b.com", "B");
  manager_->MoveItemToFolder(item_a, folder_id);
  manager_->MoveItemToFolder(item_b, folder_id);

  manager_->RemoveFolder(folder_id);

  EXPECT_THAT(manager_->GetFolders(), IsEmpty());
  // Promoted, not deleted: both items still exist as kPinned records...
  EXPECT_THAT(items_->GetPinnedItems(), SizeIs(2));
  // ...and neither is a member of any (now-nonexistent) folder.
  EXPECT_EQ(manager_->GetFolderIdForItem(item_a), "");
  EXPECT_EQ(manager_->GetFolderIdForItem(item_b), "");
}

TEST_F(PinnedFoldersManagerTest, DeleteFolderPreservesRelativeOrderOfPromoted) {
  const std::string top_level = items_->AddPinnedItem("https://existing.com", "Existing");
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_a = items_->AddPinnedItem("https://a.com", "A");
  const std::string item_b = items_->AddPinnedItem("https://b.com", "B");
  // Folder order: B then A (deliberately reversed from creation order).
  manager_->MoveItemToFolder(item_b, folder_id);
  manager_->MoveItemToFolder(item_a, folder_id);
  ASSERT_THAT(MembersOf(folder_id), ElementsAre(item_b, item_a));

  manager_->RemoveFolder(folder_id);

  // Existing top-level item keeps its place; promoted items follow in their
  // prior in-folder order (B, then A) -- not creation order (A, then B).
  std::vector<std::string> ids;
  for (const auto& item : items_->GetPinnedItems()) {
    ids.push_back(item.id);
  }
  EXPECT_THAT(ids, ElementsAre(top_level, item_b, item_a));
}

TEST_F(PinnedFoldersManagerTest, DeleteEmptyFolderIsCleanNoOpOnItems) {
  const std::string folder_id = manager_->AddFolder("Empty");
  observer_.Reset();

  manager_->RemoveFolder(folder_id);

  EXPECT_THAT(manager_->GetFolders(), IsEmpty());
}

TEST_F(PinnedFoldersManagerTest, DeleteNonexistentFolderIsNoOp) {
  observer_.Reset();
  manager_->RemoveFolder("nonexistent");
  EXPECT_EQ(observer_.change_count(), 0);
}

// ── Rename / reorder folders ─────────────────────────────────────────────────

TEST_F(PinnedFoldersManagerTest, RenameFolderChangesOnlyName) {
  const std::string folder_id = manager_->AddFolder("Old Name");
  const std::string item_id = items_->AddPinnedItem("https://a.com", "A");
  manager_->MoveItemToFolder(item_id, folder_id);

  manager_->RenameFolder(folder_id, "New Name");

  const auto folders = manager_->GetFolders();
  ASSERT_THAT(folders, SizeIs(1));
  EXPECT_EQ(folders[0].id, folder_id);
  EXPECT_EQ(folders[0].name, "New Name");
  EXPECT_THAT(folders[0].ordered_item_ids, ElementsAre(item_id));
}

TEST_F(PinnedFoldersManagerTest, ReorderFolderChangesListPosition) {
  const std::string folder_a = manager_->AddFolder("A");
  const std::string folder_b = manager_->AddFolder("B");
  const std::string folder_c = manager_->AddFolder("C");

  manager_->ReorderFolder(folder_a, 2);

  const auto folders = manager_->GetFolders();
  ASSERT_THAT(folders, SizeIs(3));
  EXPECT_EQ(folders[0].id, folder_b);
  EXPECT_EQ(folders[1].id, folder_c);
  EXPECT_EQ(folders[2].id, folder_a);
}

TEST_F(PinnedFoldersManagerTest, SetFolderExpandedPersists) {
  const std::string folder_id = manager_->AddFolder("Development");
  manager_->SetFolderExpanded(folder_id, false);
  SimulateRestart();

  const auto folders = manager_->GetFolders();
  ASSERT_THAT(folders, SizeIs(1));
  EXPECT_FALSE(folders[0].expanded);
}

// ── Move to Space ────────────────────────────────────────────────────────────

TEST_F(PinnedFoldersManagerTest, MoveToSpaceDropsFolderMembership) {
  // Folders belong to a single Space, so an item leaving that Space cannot
  // stay a member of one -- otherwise the folder it left keeps an id whose
  // item is gone.
  const std::string space_a = items_->GetActiveSpaceId();
  ASSERT_FALSE(space_a.empty());
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id =
      items_->AddPinnedItem("https://github.com", "GitHub");
  manager_->MoveItemToFolder(item_id, folder_id);

  SpaceManager space_manager(&prefs_);
  const std::string space_b =
      space_manager.CreateSpace("Other Space", "", "", "");

  MovePinnedItemToSpace(&prefs_, item_id, space_a, space_b);

  EXPECT_THAT(MembersOf(folder_id), IsEmpty());
  EXPECT_EQ(manager_->GetFolderIdForItem(item_id), "");

  // The item itself survives -- it moved, it was not deleted.
  EXPECT_THAT(items_->GetPinnedItems(), IsEmpty());
  items_->SetWindowActiveSpaceId(space_b);
  const auto moved = items_->GetPinnedItems();
  ASSERT_THAT(moved, SizeIs(1));
  EXPECT_EQ(moved[0].id, item_id);
  EXPECT_EQ(moved[0].url, "https://github.com");
}

TEST_F(PinnedFoldersManagerTest, MoveToSpaceLeavesTheFoldersOtherMembers) {
  const std::string space_a = items_->GetActiveSpaceId();
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string leaving = items_->AddPinnedItem("https://a.com", "A");
  const std::string staying = items_->AddPinnedItem("https://b.com", "B");
  manager_->MoveItemToFolder(leaving, folder_id);
  manager_->MoveItemToFolder(staying, folder_id);

  SpaceManager space_manager(&prefs_);
  const std::string space_b =
      space_manager.CreateSpace("Other Space", "", "", "");

  MovePinnedItemToSpace(&prefs_, leaving, space_a, space_b);

  EXPECT_THAT(MembersOf(folder_id), ElementsAre(staying));
}

TEST_F(PinnedFoldersManagerTest, MoveToSpaceSurvivesRestart) {
  // The scrub and the reassignment are both persisted, not just in-memory --
  // a folder must not re-adopt the item on the next launch.
  const std::string space_a = items_->GetActiveSpaceId();
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://a.com", "A");
  manager_->MoveItemToFolder(item_id, folder_id);

  SpaceManager space_manager(&prefs_);
  const std::string space_b =
      space_manager.CreateSpace("Other Space", "", "", "");
  MovePinnedItemToSpace(&prefs_, item_id, space_a, space_b);

  SimulateRestart();

  EXPECT_THAT(MembersOf(folder_id), IsEmpty());
  items_->SetWindowActiveSpaceId(space_b);
  EXPECT_THAT(items_->GetPinnedItems(), SizeIs(1));
}

TEST_F(PinnedFoldersManagerTest, MoveToSpaceIsIdempotent) {
  // Both the sidebar row and AvoraSpaceTabFilter run this for the same move,
  // so the second pass has to be a clean no-op rather than a second write.
  const std::string space_a = items_->GetActiveSpaceId();
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://a.com", "A");
  manager_->MoveItemToFolder(item_id, folder_id);

  SpaceManager space_manager(&prefs_);
  const std::string space_b =
      space_manager.CreateSpace("Other Space", "", "", "");

  MovePinnedItemToSpace(&prefs_, item_id, space_a, space_b);
  MovePinnedItemToSpace(&prefs_, item_id, space_a, space_b);

  EXPECT_THAT(MembersOf(folder_id), IsEmpty());
  items_->SetWindowActiveSpaceId(space_b);
  EXPECT_THAT(items_->GetPinnedItems(), SizeIs(1));
}

TEST_F(PinnedFoldersManagerTest, MoveToSameSpaceChangesNothing) {
  const std::string space_a = items_->GetActiveSpaceId();
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://a.com", "A");
  manager_->MoveItemToFolder(item_id, folder_id);

  MovePinnedItemToSpace(&prefs_, item_id, space_a, space_a);

  // Membership is untouched: this is not a move, so nothing is scrubbed.
  EXPECT_THAT(MembersOf(folder_id), ElementsAre(item_id));
  EXPECT_THAT(items_->GetPinnedItems(), SizeIs(1));
}

TEST_F(PinnedFoldersManagerTest, MoveToSpaceWithUnknownSourceSkipsTheScrub) {
  // An empty source means "not known", and the documented contract is to
  // leave folders alone rather than rewrite some other Space's.
  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://a.com", "A");
  manager_->MoveItemToFolder(item_id, folder_id);

  SpaceManager space_manager(&prefs_);
  const std::string space_b =
      space_manager.CreateSpace("Other Space", "", "", "");

  MovePinnedItemToSpace(&prefs_, item_id, std::string(), space_b);

  EXPECT_THAT(MembersOf(folder_id), ElementsAre(item_id));
  items_->SetWindowActiveSpaceId(space_b);
  EXPECT_THAT(items_->GetPinnedItems(), SizeIs(1));
}

// ── Cross-instance sync (two windows sharing persistent organization) ──────

TEST_F(PinnedFoldersManagerTest, CrossInstanceSync) {
  auto second_manager = CreateSecondManager();
  TestObserver second_observer;
  second_manager->AddObserver(&second_observer);

  const std::string folder_id = manager_->AddFolder("Development");
  const std::string item_id = items_->AddPinnedItem("https://a.com", "A");
  manager_->MoveItemToFolder(item_id, folder_id);

  EXPECT_GT(second_observer.change_count(), 0);
  bool found = false;
  for (const auto& folder : second_manager->GetFolders()) {
    if (folder.id == folder_id) {
      EXPECT_THAT(folder.ordered_item_ids, ElementsAre(item_id));
      found = true;
    }
  }
  EXPECT_TRUE(found);

  second_manager->RemoveObserver(&second_observer);
}

// ── Schema migration ─────────────────────────────────────────────────────────

class PinnedFoldersManagerMigrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SpaceManager::RegisterProfilePrefs(prefs_.registry());
    SidebarItemStore::RegisterProfilePrefs(prefs_.registry());
    PinnedItemsManager::RegisterProfilePrefs(prefs_.registry());
    PinnedFoldersManager::RegisterProfilePrefs(prefs_.registry());
  }

  // Writes pre-Phase-3 folder data directly (no id, {url,title} tabs list)
  // for a Space, bypassing the manager entirely -- exactly what an
  // upgrading user's existing prefs would contain.
  void SeedLegacyFolder(const std::string& space_id,
                       const std::string& name,
                       const std::vector<std::pair<std::string, std::string>>&
                           url_title_pairs) {
    base::ListValue tabs;
    for (const auto& [url, title] : url_title_pairs) {
      base::DictValue tab;
      tab.Set("url", url);
      tab.Set("title", title);
      tabs.Append(std::move(tab));
    }
    base::DictValue folder;
    folder.Set("name", name);
    folder.Set("expanded", true);
    folder.Set("tabs", std::move(tabs));

    base::ListValue folders;
    folders.Append(std::move(folder));

    ScopedDictPrefUpdate update(&prefs_,
                               PinnedFoldersManager::kPinnedFoldersBySpacePref);
    update->Set(space_id, std::move(folders));
  }

  std::string ActiveSpaceIdFor(PrefService* prefs) {
    SpaceManager space_manager(prefs);
    const Space* active = space_manager.GetActiveSpace();
    return active ? active->id : std::string();
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(PinnedFoldersManagerMigrationTest,
      LegacyFolderGetsStableIdAndItemBackedMembership) {
  const std::string space_id = ActiveSpaceIdFor(&prefs_);
  SeedLegacyFolder(space_id, "Development",
                   {{"https://github.com", "GitHub"},
                    {"https://stackoverflow.com", "Stack Overflow"}});

  PinnedFoldersManager manager(&prefs_);
  PinnedItemsManager items(&prefs_);

  const auto folders = manager.GetFolders();
  ASSERT_THAT(folders, SizeIs(1));
  EXPECT_FALSE(folders[0].id.empty());
  EXPECT_EQ(folders[0].name, "Development");
  ASSERT_THAT(folders[0].ordered_item_ids, SizeIs(2));

  // Each legacy {url,title} resolved to a real kPinned item.
  const auto pinned_items = items.GetPinnedItems();
  ASSERT_THAT(pinned_items, SizeIs(2));
  std::set<std::string> urls;
  for (const auto& item : pinned_items) {
    urls.insert(item.url);
  }
  EXPECT_THAT(urls, testing::UnorderedElementsAre("https://github.com",
                                                  "https://stackoverflow.com"));
}

TEST_F(PinnedFoldersManagerMigrationTest, MigrationIsIdempotent) {
  const std::string space_id = ActiveSpaceIdFor(&prefs_);
  SeedLegacyFolder(space_id, "Development", {{"https://github.com", "GitHub"}});

  std::string folder_id_after_first;
  std::string item_id_after_first;
  {
    PinnedFoldersManager manager(&prefs_);
    PinnedItemsManager items(&prefs_);
    ASSERT_THAT(manager.GetFolders(), SizeIs(1));
    folder_id_after_first = manager.GetFolders()[0].id;
    ASSERT_THAT(items.GetPinnedItems(), SizeIs(1));
    item_id_after_first = items.GetPinnedItems()[0].id;
  }

  // Recreate both managers (mirrors a second window, or a restart) without
  // any intervening mutation -- the migration flag must prevent it from
  // running again and re-deriving (potentially different) ids.
  {
    PinnedFoldersManager manager(&prefs_);
    PinnedItemsManager items(&prefs_);
    ASSERT_THAT(manager.GetFolders(), SizeIs(1));
    EXPECT_EQ(manager.GetFolders()[0].id, folder_id_after_first);
    ASSERT_THAT(items.GetPinnedItems(), SizeIs(1));
    EXPECT_EQ(items.GetPinnedItems()[0].id, item_id_after_first);
  }
}

TEST_F(PinnedFoldersManagerMigrationTest, MalformedEntryIsPreservedNotDropped) {
  const std::string space_id = ActiveSpaceIdFor(&prefs_);
  {
    ScopedDictPrefUpdate update(
        &prefs_, PinnedFoldersManager::kPinnedFoldersBySpacePref);
    // A non-list value under a Space key -- should never happen via the
    // manager's own API, but migration must not crash or silently drop it.
    update->Set(space_id, "not-a-list");
  }

  PinnedFoldersManager manager(&prefs_);
  EXPECT_THAT(manager.GetFolders(), IsEmpty());  // Not parseable as folders...

  // ...but the raw pref value itself was preserved, not overwritten with an
  // empty list.
  const base::DictValue& by_space =
      prefs_.GetDict(PinnedFoldersManager::kPinnedFoldersBySpacePref);
  const std::string* preserved = by_space.FindString(space_id);
  ASSERT_TRUE(preserved);
  EXPECT_EQ(*preserved, "not-a-list");
}

}  // namespace
}  // namespace avora
