// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_shared_models.h"

#include <string>

#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/task_environment.h"
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

}  // namespace
}  // namespace avora::sync
