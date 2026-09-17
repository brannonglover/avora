// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_pinned_items.h"

#include <memory>
#include <string>

#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Observer that counts notifications.
class TestObserver : public PinnedItemsManager::Observer {
 public:
  void OnPinnedItemsChanged() override { ++change_count_; }
  int change_count() const { return change_count_; }
  void Reset() { change_count_ = 0; }

 private:
  int change_count_ = 0;
};

class PinnedItemsManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SpaceManager::RegisterProfilePrefs(prefs_.registry());
    SidebarItemStore::RegisterProfilePrefs(prefs_.registry());
    manager_ = std::make_unique<PinnedItemsManager>(&prefs_);
    manager_->AddObserver(&observer_);
  }

  void TearDown() override { manager_->RemoveObserver(&observer_); }

  // Create a second manager over the same PrefService to test cross-instance
  // sync (like two windows observing the same profile).
  std::unique_ptr<PinnedItemsManager> CreateSecondManager() {
    return std::make_unique<PinnedItemsManager>(&prefs_);
  }

  // Simulate an application restart by destroying and recreating the
  // manager.
  void SimulateRestart() {
    manager_->RemoveObserver(&observer_);
    manager_.reset();
    manager_ = std::make_unique<PinnedItemsManager>(&prefs_);
    manager_->AddObserver(&observer_);
  }

  TestingPrefServiceSimple prefs_;
  std::unique_ptr<PinnedItemsManager> manager_;
  TestObserver observer_;
};

// ── Basic add / get ─────────────────────────────────────────────────────────

TEST_F(PinnedItemsManagerTest, EmptyManagerReturnsNothing) {
  EXPECT_THAT(manager_->GetPinnedItems(), IsEmpty());
  EXPECT_FALSE(manager_->IsPinned("https://example.com"));
  EXPECT_EQ(manager_->GetPinnedItemIdForUrl("https://example.com"), "");
  EXPECT_EQ(manager_->GetPinnedItemUrlById("nonexistent"), "");
}

TEST_F(PinnedItemsManagerTest, AddPinnedItemAndRetrieve) {
  const std::string id =
      manager_->AddPinnedItem("https://github.com", "GitHub");
  EXPECT_FALSE(id.empty());

  EXPECT_TRUE(manager_->IsPinned("https://github.com"));
  EXPECT_EQ(manager_->GetPinnedItemIdForUrl("https://github.com"), id);
  EXPECT_EQ(manager_->GetPinnedItemUrlById(id), "https://github.com");

  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_EQ(items[0].url, "https://github.com");
  EXPECT_EQ(items[0].title, "GitHub");
  EXPECT_EQ(items[0].id, id);
}

TEST_F(PinnedItemsManagerTest, AddPinnedItemDuplicateUrlIsNoOp) {
  const std::string first_id =
      manager_->AddPinnedItem("https://github.com", "GitHub");
  ASSERT_FALSE(first_id.empty());

  const std::string second_id =
      manager_->AddPinnedItem("https://github.com", "GitHub Again");
  EXPECT_TRUE(second_id.empty());

  EXPECT_THAT(manager_->GetPinnedItems(), SizeIs(1));
}

TEST_F(PinnedItemsManagerTest, InsertPinnedItemAtInsertsAtPosition) {
  manager_->AddPinnedItem("https://a.com", "A");
  manager_->AddPinnedItem("https://c.com", "C");
  manager_->InsertPinnedItemAt(1, "https://b.com", "B");

  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(3));
  EXPECT_EQ(items[0].url, "https://a.com");
  EXPECT_EQ(items[1].url, "https://b.com");
  EXPECT_EQ(items[2].url, "https://c.com");
}

TEST_F(PinnedItemsManagerTest, InsertPinnedItemAtDuplicateUrlIsNoOp) {
  manager_->AddPinnedItem("https://a.com", "A");
  const std::string id = manager_->InsertPinnedItemAt(0, "https://a.com", "A");
  EXPECT_TRUE(id.empty());
  EXPECT_THAT(manager_->GetPinnedItems(), SizeIs(1));
}

// ── Remove / reorder ─────────────────────────────────────────────────────────

TEST_F(PinnedItemsManagerTest, RemovePinnedItem) {
  const std::string id =
      manager_->AddPinnedItem("https://github.com", "GitHub");
  manager_->AddPinnedItem("https://gmail.com", "Gmail");

  manager_->RemovePinnedItem(id);

  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_EQ(items[0].url, "https://gmail.com");
}

TEST_F(PinnedItemsManagerTest, MovePinnedItemReorders) {
  manager_->AddPinnedItem("https://a.com", "A");
  manager_->AddPinnedItem("https://b.com", "B");
  manager_->AddPinnedItem("https://c.com", "C");

  manager_->MovePinnedItem(0, 2);

  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(3));
  EXPECT_EQ(items[0].url, "https://b.com");
  EXPECT_EQ(items[1].url, "https://c.com");
  EXPECT_EQ(items[2].url, "https://a.com");
}

TEST_F(PinnedItemsManagerTest, MovePinnedItemOutOfRangeIsNoOp) {
  manager_->AddPinnedItem("https://a.com", "A");

  manager_->MovePinnedItem(0, 5);
  manager_->MovePinnedItem(-1, 0);

  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_EQ(items[0].url, "https://a.com");
}

// ── Space scoping ─────────────────────────────────────────────────────────

TEST_F(PinnedItemsManagerTest, ItemsAreScopedToActiveSpace) {
  const std::string space_a = manager_->GetActiveSpaceId();
  ASSERT_FALSE(space_a.empty());

  manager_->AddPinnedItem("https://a.com", "A");

  SpaceManager space_manager(&prefs_);
  const std::string space_b =
      space_manager.CreateSpace("Other Space", "", "", "");
  manager_->SetWindowActiveSpaceId(space_b);

  EXPECT_THAT(manager_->GetPinnedItems(), IsEmpty());
  manager_->AddPinnedItem("https://b.com", "B");
  EXPECT_THAT(manager_->GetPinnedItems(), SizeIs(1));

  manager_->SetWindowActiveSpaceId(space_a);
  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_EQ(items[0].url, "https://a.com");
}

TEST_F(PinnedItemsManagerTest, MoveItemToSpaceReassignsWithoutRemoving) {
  const std::string space_a = manager_->GetActiveSpaceId();
  const std::string id = manager_->AddPinnedItem("https://a.com", "A");

  SpaceManager space_manager(&prefs_);
  const std::string space_b =
      space_manager.CreateSpace("Other Space", "", "", "");

  manager_->MoveItemToSpace(id, space_b);
  EXPECT_THAT(manager_->GetPinnedItems(), IsEmpty());

  manager_->SetWindowActiveSpaceId(space_b);
  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_EQ(items[0].url, "https://a.com");

  manager_->SetWindowActiveSpaceId(space_a);
}

// ── Independence from Favorites ──────────────────────────────────────────────

TEST_F(PinnedItemsManagerTest, PinnedItemsDoNotAppearAsFavorites) {
  manager_->AddPinnedItem("https://github.com", "GitHub");

  SidebarItemStore item_store(&prefs_);
  EXPECT_THAT(
      item_store.GetItems(manager_->GetActiveSpaceId(),
                          SidebarItemType::kFavorite),
      IsEmpty());
  EXPECT_THAT(
      item_store.GetItems(manager_->GetActiveSpaceId(),
                          SidebarItemType::kPinned),
      SizeIs(1));
}

// ── Persistence / reload ────────────────────────────────────────────────────

TEST_F(PinnedItemsManagerTest, SurvivesRestart) {
  manager_->AddPinnedItem("https://github.com", "GitHub");
  SimulateRestart();

  const auto items = manager_->GetPinnedItems();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_EQ(items[0].url, "https://github.com");
  EXPECT_EQ(items[0].title, "GitHub");
}

// ── Observer notifications ──────────────────────────────────────────────────

TEST_F(PinnedItemsManagerTest, ObserverNotifiedOnAdd) {
  observer_.Reset();
  manager_->AddPinnedItem("https://github.com", "GitHub");
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(PinnedItemsManagerTest, ObserverNotifiedOnRemove) {
  const std::string id =
      manager_->AddPinnedItem("https://github.com", "GitHub");
  observer_.Reset();
  manager_->RemovePinnedItem(id);
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(PinnedItemsManagerTest, CrossInstanceSync) {
  auto second_manager = CreateSecondManager();
  TestObserver second_observer;
  second_manager->AddObserver(&second_observer);

  manager_->AddPinnedItem("https://github.com", "GitHub");

  EXPECT_EQ(second_observer.change_count(), 1);
  EXPECT_THAT(second_manager->GetPinnedItems(), SizeIs(1));

  second_manager->RemoveObserver(&second_observer);
}

// ── No prefs registered ─────────────────────────────────────────────────────

// Without a registered backing pref, SidebarItemStore (like every other
// store in this codebase, e.g. ImportedLinkStore) still updates its
// in-memory cache on a write -- there is nowhere to persist to, but nothing
// crashes, and the write is visible for the lifetime of that one instance.
// What does NOT happen is persistence: a second instance created without a
// registered pref sees none of the first instance's writes.
TEST(PinnedItemsManagerNoPrefTest, WorksWithoutRegisteredPrefs) {
  TestingPrefServiceSimple prefs;
  // Deliberately do NOT register prefs.
  PinnedItemsManager manager(&prefs);

  EXPECT_THAT(manager.GetPinnedItems(), IsEmpty());

  const std::string id = manager.AddPinnedItem("https://a.com", "A");
  EXPECT_FALSE(id.empty());
  EXPECT_THAT(manager.GetPinnedItems(), SizeIs(1));

  // A second instance over the same (still-unregistered) PrefService has no
  // way to see the first instance's in-memory-only write.
  PinnedItemsManager second_manager(&prefs);
  EXPECT_THAT(second_manager.GetPinnedItems(), IsEmpty());
}

}  // namespace
}  // namespace avora
