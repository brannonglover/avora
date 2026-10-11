// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/avora_import_coordinator.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/browser/avora/avora_import_provenance.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_pinned_items.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::SizeIs;

// The Arc folder id the fixture below uses, for the provenance lookup that
// checks a re-import rebound to the folder it rebuilt.
constexpr char kArcFolderId[] = "ARC-FOLDER-GEP";

// A minimal StorableSidebar.json: one Space ("Work") whose pinned container
// holds one folder ("GEP") holding one tab.  Written out in Arc's own
// alternating id/object layout -- see arc_sidebar_parser.h for why that
// matters.
constexpr char kSidebarJson[] = R"JSON({
  "sidebar": {
    "containers": [
      {
        "spaces": [
          "ARC-SPACE-1",
          {
            "id": "ARC-SPACE-1",
            "title": "Work",
            "containerIDs": ["pinned", "ARC-PINNED-CONTAINER"]
          }
        ],
        "items": [
          "ARC-PINNED-CONTAINER",
          {
            "id": "ARC-PINNED-CONTAINER",
            "childrenIds": ["ARC-FOLDER-GEP"],
            "data": {"itemContainer": {}}
          },
          "ARC-FOLDER-GEP",
          {
            "id": "ARC-FOLDER-GEP",
            "title": "GEP",
            "childrenIds": ["ARC-TAB-1"],
            "data": {"list": {}}
          },
          "ARC-TAB-1",
          {
            "id": "ARC-TAB-1",
            "title": "EPV & UAT",
            "data": {"tab": {"savedURL": "https://example.test/epv"}}
          }
        ]
      }
    ]
  }
})JSON";

class ArcImportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SpaceManager::RegisterProfilePrefs(prefs_.registry());
    SidebarItemStore::RegisterProfilePrefs(prefs_.registry());
    PinnedItemsManager::RegisterProfilePrefs(prefs_.registry());
    PinnedFoldersManager::RegisterProfilePrefs(prefs_.registry());
    ImportProvenanceStore::RegisterProfilePrefs(prefs_.registry());

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    sidebar_path_ = temp_dir_.GetPath().AppendASCII("StorableSidebar.json");
    ASSERT_TRUE(base::WriteFile(sidebar_path_, kSidebarJson));

    space_manager_ = std::make_unique<SpaceManager>(&prefs_);
    pinned_items_ = std::make_unique<PinnedItemsManager>(&prefs_);
    pinned_folders_ = std::make_unique<PinnedFoldersManager>(&prefs_);
    provenance_ = std::make_unique<ImportProvenanceStore>(&prefs_);
  }

  ArcImportResult Import() {
    return AvoraImportCoordinator::ImportArcSidebar(
        sidebar_path_, space_manager_.get(), pinned_items_.get(),
        pinned_folders_.get(), provenance_.get());
  }

  // Scopes both managers to |space_id| the way the sidebar does for the
  // Space the window is showing.
  void LookAt(const std::string& space_id) {
    pinned_items_->SetWindowActiveSpaceId(space_id);
    pinned_folders_->SetWindowActiveSpaceId(space_id);
  }

  std::vector<PinnedFolder> FoldersNamed(const std::string& name) {
    std::vector<PinnedFolder> matches;
    for (const auto& folder : pinned_folders_->GetFolders()) {
      if (folder.name == name) {
        matches.push_back(folder);
      }
    }
    return matches;
  }

  TestingPrefServiceSimple prefs_;
  base::ScopedTempDir temp_dir_;
  base::FilePath sidebar_path_;
  std::unique_ptr<SpaceManager> space_manager_;
  std::unique_ptr<PinnedItemsManager> pinned_items_;
  std::unique_ptr<PinnedFoldersManager> pinned_folders_;
  std::unique_ptr<ImportProvenanceStore> provenance_;
};

TEST_F(ArcImportTest, FirstImportCreatesSpaceFolderAndItem) {
  const ArcImportResult result = Import();
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.spaces_created, 1);
  EXPECT_EQ(result.folders_created, 1);
  EXPECT_EQ(result.items_created, 1);
  ASSERT_THAT(result.space_ids, SizeIs(1));

  LookAt(result.space_ids[0]);
  ASSERT_THAT(FoldersNamed("GEP"), SizeIs(1));
  EXPECT_THAT(FoldersNamed("GEP")[0].ordered_item_ids, SizeIs(1));
}

// The regression this file exists for: an Avora Space that already has a
// folder by the Arc folder's name must be merged into, not shadowed by a
// second folder with the same name.
TEST_F(ArcImportTest, MergesIntoExistingSameNamedFolder) {
  const std::string space_id = space_manager_->CreateSpace("Work");
  LookAt(space_id);
  const std::string existing_folder = pinned_folders_->AddFolder("GEP");
  const std::string existing_member = pinned_items_->AddPinnedItem(
      "https://example.test/standards", "UEP Accessibility Standards");
  pinned_folders_->MoveItemToFolder(existing_member, existing_folder);

  const ArcImportResult result = Import();
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.spaces_created, 0);
  EXPECT_EQ(result.spaces_updated, 1);
  EXPECT_EQ(result.folders_created, 0);
  EXPECT_EQ(result.folders_updated, 1);

  LookAt(space_id);
  const std::vector<PinnedFolder> gep = FoldersNamed("GEP");
  ASSERT_THAT(gep, SizeIs(1)) << "the import created a second GEP folder";
  EXPECT_EQ(gep[0].id, existing_folder);
  // The user's own pin is still there, with Arc's tab filed in beside it.
  EXPECT_THAT(gep[0].ordered_item_ids, SizeIs(2));
  EXPECT_EQ(gep[0].ordered_item_ids[0], existing_member);
}

// A URL the user has already pinned cannot be added a second time, so the
// import has to adopt the pin that exists -- otherwise the tab is dropped
// from the import entirely and never lands in its folder.
TEST_F(ArcImportTest, AdoptsAlreadyPinnedUrlIntoItsFolder) {
  const std::string space_id = space_manager_->CreateSpace("Work");
  LookAt(space_id);
  const std::string existing_item =
      pinned_items_->AddPinnedItem("https://example.test/epv", "EPV");

  const ArcImportResult result = Import();
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.items_created, 0);
  EXPECT_EQ(result.items_updated, 1);

  LookAt(space_id);
  ASSERT_THAT(FoldersNamed("GEP"), SizeIs(1));
  EXPECT_THAT(FoldersNamed("GEP")[0].ordered_item_ids,
              ::testing::ElementsAre(existing_item));
}

TEST_F(ArcImportTest, ReimportUpdatesRatherThanDuplicates) {
  const ArcImportResult first = Import();
  ASSERT_TRUE(first.success) << first.error_message;

  const ArcImportResult second = Import();
  ASSERT_TRUE(second.success) << second.error_message;
  EXPECT_EQ(second.spaces_created, 0);
  EXPECT_EQ(second.folders_created, 0);
  EXPECT_EQ(second.items_created, 0);
  EXPECT_EQ(second.folders_updated, 1);
  EXPECT_EQ(second.items_updated, 1);

  // The profile's own "Default Space" plus the one Arc's Space landed in --
  // the second import added neither.
  EXPECT_THAT(space_manager_->GetSpaces(), SizeIs(2));
  LookAt(first.space_ids[0]);
  EXPECT_THAT(FoldersNamed("GEP"), SizeIs(1));
  EXPECT_THAT(pinned_items_->GetPinnedItems(), SizeIs(1));
}

// Provenance can outlive the folder it names, if the user deleted that
// folder.  Re-importing then has to build a new one rather than file the
// tabs into a folder id that no longer resolves.
TEST_F(ArcImportTest, DeletedFolderIsRecreatedOnReimport) {
  const ArcImportResult first = Import();
  ASSERT_TRUE(first.success) << first.error_message;
  const std::string space_id = first.space_ids[0];

  LookAt(space_id);
  const std::string imported_folder = FoldersNamed("GEP")[0].id;
  pinned_folders_->RemoveFolder(imported_folder);
  ASSERT_THAT(FoldersNamed("GEP"), SizeIs(0));

  const ArcImportResult second = Import();
  ASSERT_TRUE(second.success) << second.error_message;
  EXPECT_EQ(second.folders_created, 1);

  LookAt(space_id);
  const std::vector<PinnedFolder> gep = FoldersNamed("GEP");
  ASSERT_THAT(gep, SizeIs(1));
  EXPECT_NE(gep[0].id, imported_folder);
  // The tab is back inside the folder, not stranded at the top level.
  EXPECT_THAT(gep[0].ordered_item_ids, SizeIs(1));
  const ImportProvenance* record =
      provenance_->GetByExternalId("arc", "pinned_folder", kArcFolderId);
  ASSERT_TRUE(record);
  EXPECT_EQ(record->record_id, gep[0].id);
}

// Two Arc folders sharing a name must not both land in the same Avora
// folder; the second one takes a folder of its own.
TEST_F(ArcImportTest, SameNamedArcFoldersStayDistinct) {
  const std::string space_id = space_manager_->CreateSpace("Work");
  LookAt(space_id);
  ASSERT_FALSE(pinned_folders_->AddFolder("GEP").empty());

  // Two Arc folders, both called GEP, under one pinned container.
  const std::string two_folders = R"JSON({
    "sidebar": {"containers": [{
      "spaces": ["S1", {"id": "S1", "title": "Work",
                        "containerIDs": ["pinned", "C1"]}],
      "items": [
        "C1", {"id": "C1", "childrenIds": ["F1", "F2"],
               "data": {"itemContainer": {}}},
        "F1", {"id": "F1", "title": "GEP", "childrenIds": ["T1"],
               "data": {"list": {}}},
        "T1", {"id": "T1", "title": "One",
               "data": {"tab": {"savedURL": "https://example.test/one"}}},
        "F2", {"id": "F2", "title": "GEP", "childrenIds": ["T2"],
               "data": {"list": {}}},
        "T2", {"id": "T2", "title": "Two",
               "data": {"tab": {"savedURL": "https://example.test/two"}}}
      ]}]}
  })JSON";
  ASSERT_TRUE(base::WriteFile(sidebar_path_, two_folders));

  const ArcImportResult result = Import();
  ASSERT_TRUE(result.success) << result.error_message;

  LookAt(space_id);
  // The user's folder adopted by the first, a fresh one for the second.
  EXPECT_THAT(FoldersNamed("GEP"), SizeIs(2));
  EXPECT_EQ(result.folders_created, 1);
  EXPECT_EQ(result.folders_updated, 1);
  for (const auto& folder : FoldersNamed("GEP")) {
    EXPECT_THAT(folder.ordered_item_ids, SizeIs(1));
  }
}

TEST_F(ArcImportTest, UnreadableSidebarReportsFailure) {
  const ArcImportResult result = AvoraImportCoordinator::ImportArcSidebar(
      temp_dir_.GetPath().AppendASCII("missing.json"), space_manager_.get(),
      pinned_items_.get(), pinned_folders_.get(), provenance_.get());
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

}  // namespace
}  // namespace avora
