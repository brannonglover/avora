// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_shared_models.h"

#include <string>

#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/task_environment.h"
#include "chrome/browser/avora/avora_pinned_folders.h"
#include "chrome/browser/avora/avora_profile.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/avora_space_theme.h"
#include "chrome/browser/avora/sync/avora_shared_store.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora::sync {
namespace {

class AvoraSharedModelsTest : public testing::Test {
 protected:
  AvoraSharedModelsTest() {
    SpaceManager::RegisterProfilePrefs(prefs_.registry());
    SidebarItemStore::RegisterProfilePrefs(prefs_.registry());
    SharedStore::RegisterProfilePrefs(prefs_.registry());
    SharedModels::RegisterProfilePrefs(prefs_.registry());
  }

  // A server copy of Space `id`, as GET /rest/v1/spaces returns it.
  // |theme_column| is spliced in verbatim: empty leaves the column out.
  std::vector<SharedRow> ServerSpace(SharedStore& store,
                                     const std::string& id,
                                     const std::string& theme_column,
                                     const std::string& updated_at) {
    const std::string json = base::StringPrintf(
        R"([{"id": "%s", "name": "Work", "icon": "briefcase",
             "color": "#7C93FF", %s "position": "a0",
             "updated_at": "%s", "updated_by": "phone",
             "deleted_at": null}])",
        id.c_str(),
        theme_column.empty() ? "" : (theme_column + ",").c_str(),
        updated_at.c_str());
    std::optional<base::Value> rows = base::JSONReader::Read(json, 0);
    return store.MergeFromServer(SharedTable::kSpaces,
                                 std::move(*rows).TakeList());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple prefs_;
};

TEST_F(AvoraSharedModelsTest, SpacesCarryTheirTheme) {
  SharedModels models(&prefs_, base::DoNothing());
  const std::string id = models.space_manager().CreateSpace("Work");
  const SpaceTheme theme = GetSpaceThemeMoods().front().ToTheme();
  models.space_manager().SetSpaceTheme(id, theme);

  for (const LocalRow& row : models.ProjectSpaces()) {
    if (row.local_id == id) {
      EXPECT_EQ(*row.columns.FindString("theme"), theme.Serialize());
      return;
    }
  }
  ADD_FAILURE() << "Space not projected";
}

TEST_F(AvoraSharedModelsTest, RemoteThemesApply) {
  SharedModels models(&prefs_, base::DoNothing());
  SharedStore store(&prefs_);
  const std::string id = models.space_manager().CreateSpace("Work");
  store.Link(SharedTable::kSpaces, "remote", id);

  const SpaceTheme theme = GetSpaceThemeMoods().back().ToTheme();
  std::string serialized = theme.Serialize();
  base::ReplaceSubstringsAfterOffset(&serialized, 0, "\"", "\\\"");
  models.Apply(store,
               ServerSpace(store, "remote",
                           "\"theme\": \"" + serialized + "\"",
                           "2026-11-04T16:00:00+00:00"),
               {});
  EXPECT_EQ(models.space_manager().GetSpaceById(id)->theme, theme);

  // Cleared on another device: back to the default look.
  models.Apply(store,
               ServerSpace(store, "remote", R"("theme": "")",
                           "2026-11-04T16:01:00+00:00"),
               {});
  EXPECT_TRUE(models.space_manager().GetSpaceById(id)->theme.empty());
}

// A server without the column, or a row from before it, keeps the theme this
// device has.
TEST_F(AvoraSharedModelsTest, MissingThemeColumnKeepsTheTheme) {
  SharedModels models(&prefs_, base::DoNothing());
  SharedStore store(&prefs_);
  const std::string id = models.space_manager().CreateSpace("Work");
  const SpaceTheme theme = GetSpaceThemeMoods().front().ToTheme();
  models.space_manager().SetSpaceTheme(id, theme);
  store.Link(SharedTable::kSpaces, "remote", id);

  models.Apply(store,
               ServerSpace(store, "remote", std::string(),
                           "2026-11-04T16:00:00+00:00"),
               {});
  EXPECT_EQ(models.space_manager().GetSpaceById(id)->theme, theme);
}

// A pin whose URL was edited on another device ("Edit URL…") changes in
// place.  Replacing it would mint a new id, and the folder, custom name and
// open tab that refer to the old one here would lose it.
TEST_F(AvoraSharedModelsTest, RemoteUrlEditKeepsTheItem) {
  SharedModels models(&prefs_, base::DoNothing());
  SharedStore store(&prefs_);
  const std::string space = models.space_manager().CreateSpace("Work");
  store.Link(SharedTable::kSpaces, "remote-space", space);
  SidebarItemStore items(&prefs_);
  const std::string id = items.AddItem(space, SidebarItemType::kPinned,
                                       "https://old.example/", "Old");
  store.Link(SharedTable::kItems, "remote-item", id);

  std::optional<base::Value> rows = base::JSONReader::Read(
      R"([{"id": "remote-item", "space_id": "remote-space",
           "kind": "pinned", "parent_id": null,
           "url": "https://new.example/inbox", "title": "new.example/inbox",
           "position": "a0", "updated_at": "2026-11-10T16:00:00+00:00",
           "updated_by": "phone", "deleted_at": null}])",
      0);
  models.Apply(store, {},
               store.MergeFromServer(SharedTable::kItems,
                                     std::move(*rows).TakeList()));

  const SidebarItem* item = items.GetItemById(id);
  ASSERT_TRUE(item);
  EXPECT_EQ(item->url, "https://new.example/inbox");
  EXPECT_EQ(item->title, "new.example/inbox");
  EXPECT_EQ(item->type, SidebarItemType::kPinned);
  EXPECT_EQ(items.GetItems(space, SidebarItemType::kPinned).size(), 1u);
}

// Signing out needs real identities, and the pinned-folder prefs a Space's
// removal clears.
class AvoraSharedModelsSignOutTest : public AvoraSharedModelsTest {
 protected:
  AvoraSharedModelsSignOutTest() {
    BrowserProfileStore::RegisterProfilePrefs(prefs_.registry());
    PinnedFoldersManager::RegisterProfilePrefs(prefs_.registry());
  }
};

// Signing out leaves one new Space with a new identity, and sets the old
// Spaces' identities aside for the account: they come back when it signs in
// again, and never go to another account.
TEST_F(AvoraSharedModelsSignOutTest, SignOutParksIdentitiesForTheAccount) {
  SharedModels models(&prefs_, base::DoNothing());
  SpaceManager& spaces = models.space_manager();
  const std::string first = spaces.GetSpaces().front().id;
  const std::string first_identity = spaces.GetSpaces().front().profile_id;
  const std::string work = spaces.CreateSpace("Work");
  const std::string work_identity = spaces.GetSpaceById(work)->profile_id;
  SidebarItemStore items(&prefs_);
  items.AddItem(work, SidebarItemType::kPinned, "https://pinned.example/",
                "Pinned");
  SharedStore store(&prefs_);
  store.Reset("account-a");

  models.ClearForSignOut(store, "account-a");

  ASSERT_EQ(spaces.GetSpaces().size(), 1u);
  const Space fresh = spaces.GetSpaces().front();
  EXPECT_NE(fresh.id, first);
  EXPECT_NE(fresh.id, work);
  EXPECT_NE(fresh.profile_id, first_identity);
  EXPECT_NE(fresh.profile_id, work_identity);
  EXPECT_TRUE(items.GetAllItems().empty());
  // Kept, though no Space owns it now.
  EXPECT_TRUE(BrowserProfileStore(&prefs_).GetProfileById(work_identity));

  // Another account's Space of the same id gets a new identity.
  store.Reset("account-b");
  models.Apply(store,
               ServerSpace(store, work, std::string(),
                           "2026-11-20T10:00:00+00:00"),
               {});
  const std::string b_space = store.ToLocal(SharedTable::kSpaces, work);
  ASSERT_TRUE(spaces.GetSpaceById(b_space));
  EXPECT_NE(spaces.GetSpaceById(b_space)->profile_id, work_identity);
  models.ClearForSignOut(store, "account-b");

  // The account's own Space browses as it did, whether it comes down from the
  // server...
  store.Reset("account-a");
  models.Apply(store,
               ServerSpace(store, work, std::string(),
                           "2026-11-20T11:00:00+00:00"),
               {});
  const std::string restored = store.ToLocal(SharedTable::kSpaces, work);
  ASSERT_TRUE(spaces.GetSpaceById(restored));
  EXPECT_EQ(spaces.GetSpaceById(restored)->profile_id, work_identity);

  // ...or matches a Space here by name.
  ASSERT_EQ(spaces.GetSpaces().size(), 2u);
  const Space placeholder_space = spaces.GetSpaces()[0].id == restored
                                      ? spaces.GetSpaces()[1]
                                      : spaces.GetSpaces()[0];
  const std::string placeholder = placeholder_space.id;
  const std::string placeholder_identity = placeholder_space.profile_id;
  const std::string json = base::StringPrintf(
      R"([{"id": "%s", "name": "%s", "icon": "", "color": "",
           "position": "a1", "updated_at": "2026-11-20T11:00:00+00:00",
           "updated_by": "phone", "deleted_at": null}])",
      first.c_str(), placeholder_space.name.c_str());
  std::optional<base::Value> rows = base::JSONReader::Read(json, 0);
  store.MergeFromServer(SharedTable::kSpaces, std::move(*rows).TakeList());
  models.LinkMatching(store);
  EXPECT_EQ(store.ToLocal(SharedTable::kSpaces, first), placeholder);
  EXPECT_EQ(spaces.GetSpaceById(placeholder)->profile_id, first_identity);
  // The placeholder's own identity had no other Space, so it is retired.
  EXPECT_FALSE(BrowserProfileStore(&prefs_).GetProfileById(placeholder_identity));
  EXPECT_TRUE(prefs_.GetDict(SharedModels::kParkedIdentitiesPref)
                  .FindDict("account-a") == nullptr);
}

}  // namespace
}  // namespace avora::sync
