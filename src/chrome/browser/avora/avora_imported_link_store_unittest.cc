// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_imported_link_store.h"

#include <string>
#include <vector>

#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/avora/avora_imported_links.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Helpers ─────────────────────────────────────────────────────────────────────

std::string MakeId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

ImportedItem MakeLink(const std::string& id,
                      const std::string& parent_id,
                      const std::string& title,
                      const std::string& url,
                      int order) {
  ImportedItem item;
  item.id = id;
  item.parent_id = parent_id;
  item.type = ImportedItemType::kLink;
  item.title = title;
  item.url = url;
  item.order = order;
  return item;
}

ImportedItem MakeFolder(const std::string& id,
                        const std::string& parent_id,
                        const std::string& title,
                        int order) {
  ImportedItem item;
  item.id = id;
  item.parent_id = parent_id;
  item.type = ImportedItemType::kFolder;
  item.title = title;
  item.order = order;
  return item;
}

ImportedSource MakeSource(const std::string& space_id,
                          const std::string& browser,
                          const std::string& profile_name,
                          std::vector<ImportedItem> items) {
  ImportedSource source;
  source.id = MakeId();
  source.space_id = space_id;
  source.browser = browser;
  source.profile_name = profile_name;
  source.imported_at = base::Time::Now();
  source.items = std::move(items);
  return source;
}

// Observer that counts notifications.
class TestObserver : public ImportedLinkStore::Observer {
 public:
  void OnImportedLinksChanged() override { ++change_count_; }
  int change_count() const { return change_count_; }
  void Reset() { change_count_ = 0; }

 private:
  int change_count_ = 0;
};

// Test fixture ────────────────────────────────────────────────────────────────

class ImportedLinkStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ImportedLinkStore::RegisterProfilePrefs(prefs_.registry());
    store_ = std::make_unique<ImportedLinkStore>(&prefs_);
    store_->AddObserver(&observer_);
  }

  void TearDown() override { store_->RemoveObserver(&observer_); }

  // Create a second store over the same PrefService to test cross-instance
  // sync (like two windows observing the same profile).
  std::unique_ptr<ImportedLinkStore> CreateSecondStore() {
    return std::make_unique<ImportedLinkStore>(&prefs_);
  }

  // Simulate an application restart by destroying and recreating the store.
  void SimulateRestart() {
    store_->RemoveObserver(&observer_);
    store_.reset();
    store_ = std::make_unique<ImportedLinkStore>(&prefs_);
    store_->AddObserver(&observer_);
  }

  TestingPrefServiceSimple prefs_;
  std::unique_ptr<ImportedLinkStore> store_;
  TestObserver observer_;
};

// ── Basic add / get ─────────────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, EmptyStoreReturnsNothing) {
  EXPECT_THAT(store_->GetAllSources(), IsEmpty());
  EXPECT_THAT(store_->GetSourcesForSpace("space1"), IsEmpty());
  EXPECT_EQ(store_->GetSourceById("nonexistent"), nullptr);
  EXPECT_THAT(store_->GetChildren("nonexistent", ""), IsEmpty());
  EXPECT_EQ(store_->GetItemById("nonexistent", "item1"), nullptr);
}

TEST_F(ImportedLinkStoreTest, AddSourceAndRetrieve) {
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeLink("l1", "", "GitHub", "https://github.com", 0),
      MakeLink("l2", "", "Gmail", "https://gmail.com", 1),
  });
  const std::string original_id = source.id;

  std::string returned_id = store_->AddSource(std::move(source));
  EXPECT_EQ(returned_id, original_id);

  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));

  const ImportedSource* retrieved = store_->GetSourceById(original_id);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->space_id, "space1");
  EXPECT_EQ(retrieved->browser, "chrome");
  EXPECT_EQ(retrieved->profile_name, "Default");
  EXPECT_EQ(retrieved->items.size(), 2u);
}

TEST_F(ImportedLinkStoreTest, AddSourceGeneratesIdIfEmpty) {
  ImportedSource source;
  source.space_id = "space1";
  source.browser = "firefox";
  source.profile_name = "Default";
  source.imported_at = base::Time::Now();

  std::string id = store_->AddSource(std::move(source));
  EXPECT_FALSE(id.empty());

  const ImportedSource* retrieved = store_->GetSourceById(id);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->browser, "firefox");
}

TEST_F(ImportedLinkStoreTest, AddSourceGeneratesItemIdsIfEmpty) {
  ImportedItem link;
  link.type = ImportedItemType::kLink;
  link.title = "Test";
  link.url = "https://test.com";

  auto source = MakeSource("space1", "chrome", "Default", {link});

  store_->AddSource(std::move(source));

  const auto sources = store_->GetAllSources();
  ASSERT_THAT(sources, SizeIs(1));
  ASSERT_THAT(sources[0].items, SizeIs(1));
  EXPECT_FALSE(sources[0].items[0].id.empty());
}

// ── Multiple sources in one Space ───────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, MultipleSourcesInOneSpace) {
  auto chrome_source = MakeSource("space1", "chrome", "Default", {
      MakeLink(MakeId(), "", "GitHub", "https://github.com", 0),
  });
  auto firefox_source = MakeSource("space1", "firefox", "Default", {
      MakeLink(MakeId(), "", "MDN", "https://developer.mozilla.org", 0),
  });

  store_->AddSource(std::move(chrome_source));
  store_->AddSource(std::move(firefox_source));

  EXPECT_THAT(store_->GetAllSources(), SizeIs(2));
  EXPECT_THAT(store_->GetSourcesForSpace("space1"), SizeIs(2));
}

// ── Sources belonging to different Spaces ───────────────────────────────────

TEST_F(ImportedLinkStoreTest, SourcesInDifferentSpaces) {
  auto source_a = MakeSource("space1", "chrome", "Default", {
      MakeLink(MakeId(), "", "GitHub", "https://github.com", 0),
  });
  auto source_b = MakeSource("space2", "chrome", "Work", {
      MakeLink(MakeId(), "", "Jira", "https://jira.com", 0),
  });

  store_->AddSource(std::move(source_a));
  store_->AddSource(std::move(source_b));

  EXPECT_THAT(store_->GetAllSources(), SizeIs(2));
  EXPECT_THAT(store_->GetSourcesForSpace("space1"), SizeIs(1));
  EXPECT_THAT(store_->GetSourcesForSpace("space2"), SizeIs(1));
  EXPECT_THAT(store_->GetSourcesForSpace("space3"), IsEmpty());

  const auto space1_sources = store_->GetSourcesForSpace("space1");
  EXPECT_EQ(space1_sources[0].profile_name, "Default");

  const auto space2_sources = store_->GetSourcesForSpace("space2");
  EXPECT_EQ(space2_sources[0].profile_name, "Work");
}

// ── Nested folder hierarchy ─────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, NestedFolderParentRelationships) {
  const std::string root_folder_id = MakeId();
  const std::string child_folder_id = MakeId();
  const std::string link1_id = MakeId();
  const std::string link2_id = MakeId();
  const std::string link3_id = MakeId();

  //  Bookmarks Bar/
  //    GitHub
  //    Work/
  //      Jira
  //  Gmail (root-level)
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeFolder(root_folder_id, "", "Bookmarks Bar", 0),
      MakeLink(link1_id, root_folder_id, "GitHub", "https://github.com", 0),
      MakeFolder(child_folder_id, root_folder_id, "Work", 1),
      MakeLink(link2_id, child_folder_id, "Jira", "https://jira.com", 0),
      MakeLink(link3_id, "", "Gmail", "https://gmail.com", 1),
  });
  const std::string source_id = source.id;

  store_->AddSource(std::move(source));

  // Root-level children: Bookmarks Bar folder + Gmail link.
  auto root_children = store_->GetChildren(source_id, "");
  ASSERT_THAT(root_children, SizeIs(2));
  EXPECT_EQ(root_children[0].title, "Bookmarks Bar");
  EXPECT_EQ(root_children[0].type, ImportedItemType::kFolder);
  EXPECT_EQ(root_children[1].title, "Gmail");
  EXPECT_EQ(root_children[1].type, ImportedItemType::kLink);

  // Bookmarks Bar children: GitHub link + Work folder.
  auto bar_children = store_->GetChildren(source_id, root_folder_id);
  ASSERT_THAT(bar_children, SizeIs(2));
  EXPECT_EQ(bar_children[0].title, "GitHub");
  EXPECT_EQ(bar_children[1].title, "Work");

  // Work folder children: Jira link.
  auto work_children = store_->GetChildren(source_id, child_folder_id);
  ASSERT_THAT(work_children, SizeIs(1));
  EXPECT_EQ(work_children[0].title, "Jira");
  EXPECT_EQ(work_children[0].url, "https://jira.com");
}

TEST_F(ImportedLinkStoreTest, GetChildrenReturnsOrderedResults) {
  const std::string folder_id = MakeId();
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeFolder(folder_id, "", "Folder", 0),
      MakeLink(MakeId(), folder_id, "C-Third", "https://c.com", 2),
      MakeLink(MakeId(), folder_id, "A-First", "https://a.com", 0),
      MakeLink(MakeId(), folder_id, "B-Second", "https://b.com", 1),
  });
  const std::string source_id = source.id;

  store_->AddSource(std::move(source));

  auto children = store_->GetChildren(source_id, folder_id);
  ASSERT_THAT(children, SizeIs(3));
  EXPECT_EQ(children[0].title, "A-First");
  EXPECT_EQ(children[1].title, "B-Second");
  EXPECT_EQ(children[2].title, "C-Third");
}

TEST_F(ImportedLinkStoreTest, GetChildrenForNonexistentSource) {
  EXPECT_THAT(store_->GetChildren("nonexistent", ""), IsEmpty());
}

// ── GetItemById ─────────────────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, GetItemById) {
  const std::string link_id = MakeId();
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeLink(link_id, "", "GitHub", "https://github.com", 0),
  });
  const std::string source_id = source.id;

  store_->AddSource(std::move(source));

  const ImportedItem* item = store_->GetItemById(source_id, link_id);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->title, "GitHub");
  EXPECT_EQ(item->url, "https://github.com");
}

TEST_F(ImportedLinkStoreTest, GetItemByIdNotFound) {
  auto source = MakeSource("space1", "chrome", "Default", {});
  const std::string source_id = source.id;
  store_->AddSource(std::move(source));

  EXPECT_EQ(store_->GetItemById(source_id, "nonexistent"), nullptr);
  EXPECT_EQ(store_->GetItemById("nonexistent", "item"), nullptr);
}

// ── Remove source without affecting others ──────────────────────────────────

TEST_F(ImportedLinkStoreTest, RemoveSourceLeavesOthersIntact) {
  auto source_a = MakeSource("space1", "chrome", "Default", {
      MakeLink(MakeId(), "", "GitHub", "https://github.com", 0),
  });
  auto source_b = MakeSource("space1", "firefox", "Default", {
      MakeLink(MakeId(), "", "MDN", "https://developer.mozilla.org", 0),
  });
  const std::string id_a = source_a.id;
  const std::string id_b = source_b.id;

  store_->AddSource(std::move(source_a));
  store_->AddSource(std::move(source_b));
  ASSERT_THAT(store_->GetAllSources(), SizeIs(2));

  store_->RemoveSource(id_a);

  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));
  EXPECT_EQ(store_->GetSourceById(id_a), nullptr);

  const ImportedSource* remaining = store_->GetSourceById(id_b);
  ASSERT_NE(remaining, nullptr);
  EXPECT_EQ(remaining->browser, "firefox");
  EXPECT_THAT(remaining->items, SizeIs(1));
}

TEST_F(ImportedLinkStoreTest, RemoveSourceNonexistent) {
  observer_.Reset();
  store_->RemoveSource("nonexistent");
  EXPECT_EQ(observer_.change_count(), 0);
}

// ── Remove individual item ──────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, RemoveItemLink) {
  const std::string link1_id = MakeId();
  const std::string link2_id = MakeId();
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeLink(link1_id, "", "GitHub", "https://github.com", 0),
      MakeLink(link2_id, "", "Gmail", "https://gmail.com", 1),
  });
  const std::string source_id = source.id;

  store_->AddSource(std::move(source));
  store_->RemoveItem(source_id, link1_id);

  const ImportedSource* updated = store_->GetSourceById(source_id);
  ASSERT_NE(updated, nullptr);
  ASSERT_THAT(updated->items, SizeIs(1));
  EXPECT_EQ(updated->items[0].id, link2_id);
}

TEST_F(ImportedLinkStoreTest, RemoveFolderCascadesToDescendants) {
  const std::string folder_id = MakeId();
  const std::string child_folder_id = MakeId();
  const std::string link1_id = MakeId();
  const std::string link2_id = MakeId();
  const std::string root_link_id = MakeId();

  //  Folder/
  //    Link1
  //    ChildFolder/
  //      Link2
  //  RootLink (root-level, should survive)
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeFolder(folder_id, "", "Folder", 0),
      MakeLink(link1_id, folder_id, "Link1", "https://one.com", 0),
      MakeFolder(child_folder_id, folder_id, "ChildFolder", 1),
      MakeLink(link2_id, child_folder_id, "Link2", "https://two.com", 0),
      MakeLink(root_link_id, "", "RootLink", "https://root.com", 1),
  });
  const std::string source_id = source.id;

  store_->AddSource(std::move(source));

  // Remove the top-level folder — all nested items should go too.
  store_->RemoveItem(source_id, folder_id);

  const ImportedSource* updated = store_->GetSourceById(source_id);
  ASSERT_NE(updated, nullptr);
  ASSERT_THAT(updated->items, SizeIs(1));
  EXPECT_EQ(updated->items[0].id, root_link_id);
  EXPECT_EQ(updated->items[0].title, "RootLink");
}

TEST_F(ImportedLinkStoreTest, RemoveItemNonexistent) {
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeLink(MakeId(), "", "GitHub", "https://github.com", 0),
  });
  const std::string source_id = source.id;
  store_->AddSource(std::move(source));

  observer_.Reset();
  store_->RemoveItem(source_id, "nonexistent");
  EXPECT_EQ(observer_.change_count(), 0);

  store_->RemoveItem("nonexistent_source", "nonexistent_item");
  EXPECT_EQ(observer_.change_count(), 0);
}

// ── RemoveSourcesForSpace ───────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, RemoveSourcesForSpaceRemovesOnlyTargeted) {
  auto source_a = MakeSource("space1", "chrome", "Default", {
      MakeLink(MakeId(), "", "GitHub", "https://github.com", 0),
  });
  auto source_b = MakeSource("space1", "chrome", "Work", {
      MakeLink(MakeId(), "", "Jira", "https://jira.com", 0),
  });
  auto source_c = MakeSource("space2", "firefox", "Default", {
      MakeLink(MakeId(), "", "MDN", "https://mdn.com", 0),
  });
  const std::string id_c = source_c.id;

  store_->AddSource(std::move(source_a));
  store_->AddSource(std::move(source_b));
  store_->AddSource(std::move(source_c));
  ASSERT_THAT(store_->GetAllSources(), SizeIs(3));

  store_->RemoveSourcesForSpace("space1");

  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));
  EXPECT_THAT(store_->GetSourcesForSpace("space1"), IsEmpty());

  const ImportedSource* remaining = store_->GetSourceById(id_c);
  ASSERT_NE(remaining, nullptr);
  EXPECT_EQ(remaining->space_id, "space2");
}

TEST_F(ImportedLinkStoreTest, RemoveSourcesForSpaceEmptyIdIsNoOp) {
  auto source = MakeSource("space1", "chrome", "Default", {});
  store_->AddSource(std::move(source));

  observer_.Reset();
  store_->RemoveSourcesForSpace("");
  EXPECT_EQ(observer_.change_count(), 0);
  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));
}

TEST_F(ImportedLinkStoreTest, RemoveSourcesForNonexistentSpace) {
  auto source = MakeSource("space1", "chrome", "Default", {});
  store_->AddSource(std::move(source));

  observer_.Reset();
  store_->RemoveSourcesForSpace("nonexistent");
  EXPECT_EQ(observer_.change_count(), 0);
  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));
}

// ── Persistence / reload ────────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, SurvivesRestart) {
  const std::string folder_id = MakeId();
  const std::string link_id = MakeId();

  auto source = MakeSource("space1", "chrome", "Default", {
      MakeFolder(folder_id, "", "Bookmarks Bar", 0),
      MakeLink(link_id, folder_id, "GitHub", "https://github.com", 0),
  });
  source.browser = "chrome";
  source.profile_name = "Default";
  const std::string source_id = source.id;
  const base::Time original_time = source.imported_at;

  store_->AddSource(std::move(source));
  SimulateRestart();

  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));

  const ImportedSource* retrieved = store_->GetSourceById(source_id);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->space_id, "space1");
  EXPECT_EQ(retrieved->browser, "chrome");
  EXPECT_EQ(retrieved->profile_name, "Default");
  EXPECT_EQ(retrieved->imported_at, original_time);
  ASSERT_THAT(retrieved->items, SizeIs(2));

  // Verify folder hierarchy survived.
  auto root_children = store_->GetChildren(source_id, "");
  ASSERT_THAT(root_children, SizeIs(1));
  EXPECT_EQ(root_children[0].id, folder_id);
  EXPECT_EQ(root_children[0].type, ImportedItemType::kFolder);

  auto folder_children = store_->GetChildren(source_id, folder_id);
  ASSERT_THAT(folder_children, SizeIs(1));
  EXPECT_EQ(folder_children[0].id, link_id);
  EXPECT_EQ(folder_children[0].url, "https://github.com");
}

TEST_F(ImportedLinkStoreTest, MutationsSurviveRestart) {
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeLink(MakeId(), "", "GitHub", "https://github.com", 0),
      MakeLink(MakeId(), "", "Gmail", "https://gmail.com", 1),
  });
  const std::string source_id = source.id;
  const std::string remove_id = source.items[0].id;

  store_->AddSource(std::move(source));
  store_->RemoveItem(source_id, remove_id);

  SimulateRestart();

  const ImportedSource* retrieved = store_->GetSourceById(source_id);
  ASSERT_NE(retrieved, nullptr);
  ASSERT_THAT(retrieved->items, SizeIs(1));
  EXPECT_EQ(retrieved->items[0].url, "https://gmail.com");
}

// ── Malformed / invalid stored data ─────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, MalformedItemsAreSkippedGracefully) {
  // Manually write garbage into the pref to simulate corruption.
  base::ListValue corrupt_list;

  // A dict missing "id" — should be skipped (empty id).
  base::DictValue bad_source;
  bad_source.Set("space_id", "space1");
  bad_source.Set("browser", "chrome");
  corrupt_list.Append(std::move(bad_source));

  // A non-dict entry — should be skipped.
  corrupt_list.Append("not a dict");

  // A valid source with a mix of valid and invalid items.
  base::DictValue good_source;
  good_source.Set("id", "good-source-id");
  good_source.Set("space_id", "space1");
  good_source.Set("browser", "chrome");
  good_source.Set("profile_name", "Default");

  base::ListValue items;
  // Valid item.
  base::DictValue good_item;
  good_item.Set("id", "item1");
  good_item.Set("parent_id", "");
  good_item.Set("type", 1);
  good_item.Set("title", "Good Link");
  good_item.Set("url", "https://good.com");
  good_item.Set("order", 0);
  items.Append(std::move(good_item));

  // Item with unknown type value — should default to kLink.
  base::DictValue unknown_type_item;
  unknown_type_item.Set("id", "item2");
  unknown_type_item.Set("type", 99);
  unknown_type_item.Set("title", "Unknown Type");
  unknown_type_item.Set("url", "https://unknown.com");
  unknown_type_item.Set("order", 1);
  items.Append(std::move(unknown_type_item));

  // Non-dict item in items list — skipped.
  items.Append(42);

  good_source.Set("items", std::move(items));
  corrupt_list.Append(std::move(good_source));

  prefs_.SetList("avora.imported_links", std::move(corrupt_list));

  // Force cache refresh.
  SimulateRestart();

  // The bad source (no id) was skipped; the good source is present.
  const auto sources = store_->GetAllSources();
  ASSERT_THAT(sources, SizeIs(1));
  EXPECT_EQ(sources[0].id, "good-source-id");

  // Two items were deserialized (the non-dict was skipped).
  ASSERT_THAT(sources[0].items, SizeIs(2));
  EXPECT_EQ(sources[0].items[0].title, "Good Link");
  EXPECT_EQ(sources[0].items[1].title, "Unknown Type");
  EXPECT_EQ(sources[0].items[1].type, ImportedItemType::kLink);
}

TEST_F(ImportedLinkStoreTest, MissingFieldsGetDefaults) {
  base::ListValue list;
  base::DictValue source_dict;
  source_dict.Set("id", "src1");
  // No space_id, browser, profile_name, imported_at.
  base::ListValue items;
  base::DictValue item_dict;
  item_dict.Set("id", "item1");
  // No parent_id, type, title, url, order.
  items.Append(std::move(item_dict));
  source_dict.Set("items", std::move(items));
  list.Append(std::move(source_dict));

  prefs_.SetList("avora.imported_links", std::move(list));
  SimulateRestart();

  const auto sources = store_->GetAllSources();
  ASSERT_THAT(sources, SizeIs(1));
  EXPECT_EQ(sources[0].space_id, "");
  EXPECT_EQ(sources[0].browser, "");
  EXPECT_EQ(sources[0].profile_name, "");
  EXPECT_TRUE(sources[0].imported_at.is_null());

  ASSERT_THAT(sources[0].items, SizeIs(1));
  EXPECT_EQ(sources[0].items[0].parent_id, "");
  EXPECT_EQ(sources[0].items[0].type, ImportedItemType::kLink);
  EXPECT_EQ(sources[0].items[0].title, "");
  EXPECT_EQ(sources[0].items[0].url, "");
  EXPECT_EQ(sources[0].items[0].order, 0);
}

TEST_F(ImportedLinkStoreTest, EmptyPrefLoadsCleanly) {
  prefs_.SetList("avora.imported_links", base::ListValue());
  SimulateRestart();
  EXPECT_THAT(store_->GetAllSources(), IsEmpty());
}

// ── Observer notifications ──────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, ObserverNotifiedOnAdd) {
  observer_.Reset();
  store_->AddSource(MakeSource("space1", "chrome", "Default", {}));
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(ImportedLinkStoreTest, ObserverNotifiedOnRemoveSource) {
  auto source = MakeSource("space1", "chrome", "Default", {});
  const std::string id = source.id;
  store_->AddSource(std::move(source));

  observer_.Reset();
  store_->RemoveSource(id);
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(ImportedLinkStoreTest, ObserverNotifiedOnRemoveItem) {
  const std::string link_id = MakeId();
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeLink(link_id, "", "GitHub", "https://github.com", 0),
  });
  const std::string source_id = source.id;
  store_->AddSource(std::move(source));

  observer_.Reset();
  store_->RemoveItem(source_id, link_id);
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(ImportedLinkStoreTest, ObserverNotifiedOnRemoveSourcesForSpace) {
  store_->AddSource(MakeSource("space1", "chrome", "Default", {}));

  observer_.Reset();
  store_->RemoveSourcesForSpace("space1");
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(ImportedLinkStoreTest, CrossInstanceSync) {
  auto second_store = CreateSecondStore();
  TestObserver second_observer;
  second_store->AddObserver(&second_observer);

  // Write via the first store.
  store_->AddSource(MakeSource("space1", "chrome", "Default", {}));

  // The second store should see the change via pref watching.
  EXPECT_EQ(second_observer.change_count(), 1);
  EXPECT_THAT(second_store->GetAllSources(), SizeIs(1));

  second_store->RemoveObserver(&second_observer);
}

// ── AddSource replaces existing ─────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, AddSourceReplacesExistingWithSameId) {
  auto source = MakeSource("space1", "chrome", "Default", {
      MakeLink(MakeId(), "", "Old Link", "https://old.com", 0),
  });
  const std::string source_id = source.id;
  store_->AddSource(std::move(source));

  // Add again with the same id but different items.
  ImportedSource updated;
  updated.id = source_id;
  updated.space_id = "space1";
  updated.browser = "chrome";
  updated.profile_name = "Updated";
  updated.imported_at = base::Time::Now();
  updated.items = {
      MakeLink(MakeId(), "", "New Link 1", "https://new1.com", 0),
      MakeLink(MakeId(), "", "New Link 2", "https://new2.com", 1),
  };

  store_->AddSource(std::move(updated));

  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));
  const ImportedSource* retrieved = store_->GetSourceById(source_id);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->profile_name, "Updated");
  EXPECT_THAT(retrieved->items, SizeIs(2));
}

// ── GetSourcesForSpace ordering ─────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, SourcesForSpaceOrderedByImportTime) {
  const base::Time early = base::Time::Now() - base::Hours(2);
  const base::Time late = base::Time::Now();

  ImportedSource source_late;
  source_late.id = MakeId();
  source_late.space_id = "space1";
  source_late.browser = "chrome";
  source_late.profile_name = "Late";
  source_late.imported_at = late;

  ImportedSource source_early;
  source_early.id = MakeId();
  source_early.space_id = "space1";
  source_early.browser = "firefox";
  source_early.profile_name = "Early";
  source_early.imported_at = early;

  // Add late first, then early.
  store_->AddSource(std::move(source_late));
  store_->AddSource(std::move(source_early));

  auto sources = store_->GetSourcesForSpace("space1");
  ASSERT_THAT(sources, SizeIs(2));
  EXPECT_EQ(sources[0].profile_name, "Early");
  EXPECT_EQ(sources[1].profile_name, "Late");
}

// ── Serialization round-trip ────────────────────────────────────────────────

TEST_F(ImportedLinkStoreTest, SerializationRoundTrip) {
  const std::string folder_id = MakeId();
  ImportedSource original;
  original.id = MakeId();
  original.space_id = "space-abc";
  original.browser = "edge";
  original.profile_name = "Personal";
  original.imported_at = base::Time::Now();
  original.items = {
      MakeFolder(folder_id, "", "Bookmarks Bar", 0),
      MakeLink(MakeId(), folder_id, "GitHub", "https://github.com", 0),
      MakeLink(MakeId(), folder_id, "Stack Overflow",
               "https://stackoverflow.com", 1),
      MakeLink(MakeId(), "", "Root Link", "https://root.com", 1),
  };

  base::DictValue dict = original.ToDict();
  ImportedSource restored = ImportedSource::FromDict(dict);

  EXPECT_EQ(restored.id, original.id);
  EXPECT_EQ(restored.space_id, original.space_id);
  EXPECT_EQ(restored.browser, original.browser);
  EXPECT_EQ(restored.profile_name, original.profile_name);
  EXPECT_EQ(restored.imported_at, original.imported_at);
  ASSERT_EQ(restored.items.size(), original.items.size());

  for (size_t i = 0; i < original.items.size(); ++i) {
    EXPECT_EQ(restored.items[i].id, original.items[i].id);
    EXPECT_EQ(restored.items[i].parent_id, original.items[i].parent_id);
    EXPECT_EQ(restored.items[i].type, original.items[i].type);
    EXPECT_EQ(restored.items[i].title, original.items[i].title);
    EXPECT_EQ(restored.items[i].url, original.items[i].url);
    EXPECT_EQ(restored.items[i].order, original.items[i].order);
  }
}

// ── Space-deletion behavior (documented, not auto-destroyed) ────────────────

TEST_F(ImportedLinkStoreTest, OrphanedSourcesSurviveWhenSpaceDeleted) {
  // This test documents the current behavior: when a Space is deleted
  // externally, its imported sources remain in storage.  They become
  // invisible because no window's active_space_id will match them,
  // but they are not lost.
  //
  // The decision to delete, reassign, or retain orphaned imports is a
  // product-level choice made by the caller (e.g. the Space deletion
  // UI), not by the store itself.

  auto source = MakeSource("deleted-space", "chrome", "Default", {
      MakeLink(MakeId(), "", "GitHub", "https://github.com", 0),
  });
  const std::string source_id = source.id;
  store_->AddSource(std::move(source));

  // Simulate Space "deleted-space" being removed from SpaceManager.
  // The store knows nothing about that — it just stores data.

  // Source is invisible to any real Space...
  EXPECT_THAT(store_->GetSourcesForSpace("space1"), IsEmpty());

  // ...but retrievable by id and from GetAllSources().
  EXPECT_NE(store_->GetSourceById(source_id), nullptr);
  EXPECT_THAT(store_->GetAllSources(), SizeIs(1));

  // And explicitly removable if the caller decides to clean up.
  store_->RemoveSourcesForSpace("deleted-space");
  EXPECT_THAT(store_->GetAllSources(), IsEmpty());
}

// ── No prefs registered ─────────────────────────────────────────────────────

TEST(ImportedLinkStoreNoPrefTest, WorksWithoutRegisteredPrefs) {
  TestingPrefServiceSimple prefs;
  // Deliberately do NOT register prefs.
  ImportedLinkStore store(&prefs);

  EXPECT_THAT(store.GetAllSources(), IsEmpty());
  EXPECT_THAT(store.GetSourcesForSpace("space1"), IsEmpty());

  // Mutations are silently no-ops.
  store.AddSource(MakeSource("space1", "chrome", "Default", {}));
  EXPECT_THAT(store.GetAllSources(), IsEmpty());

  store.RemoveSource("nonexistent");
  store.RemoveSourcesForSpace("space1");
}

}  // namespace
}  // namespace avora
