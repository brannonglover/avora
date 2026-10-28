// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_shared_store.h"

#include "base/json/json_reader.h"
#include "base/test/task_environment.h"
#include "chrome/browser/avora/sync/avora_fractional_index.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora::sync {
namespace {

LocalRow Pinned(std::string id,
                std::string space,
                int index,
                std::string title = "") {
  LocalRow row;
  row.local_id = std::move(id);
  row.columns.Set("space_id", space);
  row.columns.Set("kind", "pinned");
  row.columns.Set("url", "https://" + row.local_id + ".example/");
  row.columns.Set("title", title.empty() ? row.local_id : title);
  row.container = space + "/pinned";
  row.index = index;
  return row;
}

std::vector<LocalRow> Rows(std::initializer_list<LocalRow*> rows) {
  std::vector<LocalRow> list;
  for (LocalRow* row : rows) {
    LocalRow copy;
    copy.local_id = row->local_id;
    copy.columns = row->columns.Clone();
    copy.container = row->container;
    copy.index = row->index;
    list.push_back(std::move(copy));
  }
  return list;
}

base::ListValue ServerRows(std::string_view json) {
  std::optional<base::Value> value = base::JSONReader::Read(json, 0);
  return std::move(*value).TakeList();
}

// The pending changes keyed by id, without the updated_by stamp.
std::map<std::string, base::DictValue> Pending(SharedStore& store,
                                               SharedTable table) {
  std::map<std::string, base::DictValue> changes;
  for (base::Value& change : store.TakePending(table, "me")) {
    base::DictValue dict = std::move(change).TakeDict();
    std::string id = *dict.FindString("id");
    dict.Remove("id");
    dict.Remove("updated_by");
    changes[id] = std::move(dict);
  }
  store.FinishPush(table, true);
  return changes;
}

class AvoraSharedStoreTest : public testing::Test {
 protected:
  AvoraSharedStoreTest() {
    SharedStore::RegisterProfilePrefs(prefs_.registry());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple prefs_;
};

TEST(AvoraAssignPositionsTest, KeepsAsManyPositionsAsTheOrderAllows) {
  // c moved to the front: only c needs a new position.
  std::map<std::string, std::string> assigned =
      AssignPositions({"c", "a", "b"}, {{"a", "a0"}, {"b", "a1"}, {"c", "a2"}});
  ASSERT_EQ(1u, assigned.size());
  EXPECT_LT(assigned["c"], "a0");

  // New rows go between their neighbours.
  assigned = AssignPositions({"a", "x", "b", "y"}, {{"a", "a0"}, {"b", "a1"}});
  ASSERT_EQ(2u, assigned.size());
  EXPECT_LT("a0", assigned["x"]);
  EXPECT_LT(assigned["x"], "a1");
  EXPECT_LT("a1", assigned["y"]);

  // Nothing positioned yet: ascending keys from the start.
  assigned = AssignPositions({"a", "b", "c"}, {});
  EXPECT_EQ("a0", assigned["a"]);
  EXPECT_EQ("a1", assigned["b"]);
  EXPECT_EQ("a2", assigned["c"]);

  // Duplicate or invalid positions are replaced.
  assigned = AssignPositions({"a", "b"}, {{"a", "a0"}, {"b", "a0"}});
  ASSERT_EQ(1u, assigned.size());
  const std::string a = assigned.count("a") ? assigned["a"] : "a0";
  const std::string b = assigned.count("b") ? assigned["b"] : "a0";
  EXPECT_LT(a, b);
  assigned = AssignPositions({"a"}, {{"a", "not a key"}});
  EXPECT_EQ(1u, assigned.size());
}

TEST_F(AvoraSharedStoreTest, FirstReconcileOnlyRecordsTheBaseline) {
  SharedStore store(&prefs_);
  LocalRow a = Pinned("a", "s1", 0);
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a}));
  EXPECT_FALSE(store.HasPending());
}

TEST_F(AvoraSharedStoreTest, LocalChangesBecomeColumnWrites) {
  SharedStore store(&prefs_);
  LocalRow a = Pinned("a", "s1", 0);
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a}));

  // Added.
  LocalRow b = Pinned("b", "s1", 1);
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a, &b}));
  std::map<std::string, base::DictValue> pending =
      Pending(store, SharedTable::kItems);
  ASSERT_EQ(2u, pending.size());
  EXPECT_EQ("https://b.example/", *pending["b"].FindString("url"));
  EXPECT_EQ("pinned", *pending["b"].FindString("kind"));
  // `a` had no position yet either, since it was only in the baseline.
  EXPECT_TRUE(pending["b"].FindString("position"));
  EXPECT_LT(*pending["a"].FindString("position"),
            *pending["b"].FindString("position"));

  // Retitled: only the title goes.
  b.columns.Set("title", "Bee");
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a, &b}));
  pending = Pending(store, SharedTable::kItems);
  ASSERT_EQ(1u, pending.size());
  base::DictValue retitled;
  retitled.Set("title", "Bee");
  EXPECT_EQ(retitled, pending["b"]);

  // Reordered: one row moves.
  a.index = 1;
  b.index = 0;
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a, &b}));
  pending = Pending(store, SharedTable::kItems);
  ASSERT_EQ(1u, pending.size());
  const std::string* moved = pending.begin()->second.FindString("position");
  ASSERT_TRUE(moved);
  EXPECT_LT(*store.Find(SharedTable::kItems, "b")->columns.FindString("position"),
            *store.Find(SharedTable::kItems, "a")->columns.FindString("position"));

  // Removed: tombstoned.
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&b}));
  pending = Pending(store, SharedTable::kItems);
  ASSERT_EQ(1u, pending.size());
  EXPECT_TRUE(pending["a"].FindString("deleted_at"));
  EXPECT_TRUE(store.Find(SharedTable::kItems, "a")->deleted);

  // Nothing changed: nothing sent.
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&b}));
  EXPECT_FALSE(store.HasPending());
}

TEST_F(AvoraSharedStoreTest, RemoteChangesAreNotEchoed) {
  SharedStore store(&prefs_);
  LocalRow a = Pinned("a", "s1", 0);
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a}), true);
  Pending(store, SharedTable::kItems);

  // Another device retitles `a`; the device applies it, then rebases.
  std::vector<SharedRow> changed = store.MergeFromServer(
      SharedTable::kItems, ServerRows(R"([{"id": "a", "space_id": "s1",
          "kind": "pinned", "url": "https://a.example/", "title": "Remote",
          "position": "a0", "updated_at": "2026-10-28T16:00:00+00:00",
          "updated_by": "phone", "deleted_at": null}])"));
  ASSERT_EQ(1u, changed.size());
  EXPECT_EQ("Remote", *changed[0].columns.FindString("title"));

  // Before the local model catches up, a reconcile mustn't undo it.
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a}));
  EXPECT_FALSE(store.HasPending());

  a.columns.Set("title", "Remote");
  store.Rebase(SharedTable::kItems, "pinned", Rows({&a}));
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a}));
  EXPECT_FALSE(store.HasPending());

  // The same row again (the catch-up overlap) is not a change.
  changed = store.MergeFromServer(
      SharedTable::kItems, ServerRows(R"([{"id": "a", "space_id": "s1",
          "kind": "pinned", "url": "https://a.example/", "title": "Remote",
          "position": "a0", "updated_at": "2026-10-28T16:00:00+00:00",
          "updated_by": "phone", "deleted_at": null}])"));
  EXPECT_TRUE(changed.empty());

  // An older copy is ignored.
  changed = store.MergeFromServer(
      SharedTable::kItems, ServerRows(R"([{"id": "a", "space_id": "s1",
          "kind": "pinned", "url": "https://a.example/", "title": "Old",
          "position": "a0", "updated_at": "2026-10-28T15:00:00+00:00",
          "updated_by": "phone", "deleted_at": null}])"));
  EXPECT_TRUE(changed.empty());
  EXPECT_EQ("2026-10-28T16:00:00.000000+00:00",
            FormatServerTime(*store.cursor()));
}

TEST_F(AvoraSharedStoreTest, UnsentChangesWinOverServerRows) {
  SharedStore store(&prefs_);
  base::DictValue title;
  title.Set("title", "Mine");
  store.Write(SharedTable::kItems, "a", std::move(title));

  std::vector<SharedRow> changed = store.MergeFromServer(
      SharedTable::kItems, ServerRows(R"([{"id": "a", "space_id": "s1",
          "kind": "today", "url": "https://a.example/", "title": "Theirs",
          "position": "a0", "updated_at": "2026-10-28T16:00:00+00:00",
          "updated_by": "phone", "deleted_at": null}])"));
  ASSERT_EQ(1u, changed.size());
  EXPECT_EQ("Mine", *changed[0].columns.FindString("title"));
  EXPECT_EQ("https://a.example/", *changed[0].columns.FindString("url"));

  // A failed push puts the change back, under anything newer.
  base::ListValue sent = store.TakePending(SharedTable::kItems, "me");
  ASSERT_EQ(1u, sent.size());
  EXPECT_EQ("me", *sent[0].GetDict().FindString("updated_by"));
  base::DictValue newer;
  newer.Set("title", "Newer");
  store.Write(SharedTable::kItems, "a", std::move(newer));
  store.FinishPush(SharedTable::kItems, false);
  std::map<std::string, base::DictValue> pending =
      Pending(store, SharedTable::kItems);
  EXPECT_EQ("Newer", *pending["a"].FindString("title"));
}

TEST_F(AvoraSharedStoreTest, LinksTranslateIds) {
  SharedStore store(&prefs_);
  store.Link(SharedTable::kSpaces, "shared-space", "local-space");
  EXPECT_EQ("local-space", store.ToLocal(SharedTable::kSpaces, "shared-space"));
  EXPECT_EQ("shared-space", store.ToShared(SharedTable::kSpaces, "local-space"));
  EXPECT_EQ("other", store.ToShared(SharedTable::kSpaces, "other"));

  store.Reconcile(SharedTable::kItems, "pinned", {});
  LocalRow a = Pinned("a", "local-space", 0);
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a}));
  std::map<std::string, base::DictValue> pending =
      Pending(store, SharedTable::kItems);
  EXPECT_EQ("shared-space", *pending["a"].FindString("space_id"));
}

TEST_F(AvoraSharedStoreTest, ReopeningAClosedRowUndeletesIt) {
  SharedStore store(&prefs_);
  store.MergeFromServer(
      SharedTable::kItems, ServerRows(R"([{"id": "a", "space_id": "s1",
          "kind": "pinned", "url": "https://a.example/", "title": "a",
          "position": "a0", "updated_at": "2026-10-28T16:00:00+00:00",
          "updated_by": "me",
          "deleted_at": "2026-10-28T16:00:00+00:00"}])"));
  store.Reconcile(SharedTable::kItems, "pinned", {});
  LocalRow a = Pinned("a", "s1", 0);
  store.Reconcile(SharedTable::kItems, "pinned", Rows({&a}));
  std::map<std::string, base::DictValue> pending =
      Pending(store, SharedTable::kItems);
  ASSERT_TRUE(pending["a"].Find("deleted_at"));
  EXPECT_TRUE(pending["a"].Find("deleted_at")->is_none());
}

TEST_F(AvoraSharedStoreTest, SurvivesARestart) {
  {
    SharedStore store(&prefs_);
    store.Reset("user-1");
    store.MergeFromServer(
        SharedTable::kSpaces, ServerRows(R"([{"id": "s1", "name": "Work",
            "icon": "", "color": "", "position": "a0",
            "updated_at": "2026-10-28T16:00:00+00:00", "updated_by": "phone",
            "deleted_at": null}])"));
    store.Link(SharedTable::kSpaces, "s1", "local-1");
    base::DictValue title;
    title.Set("title", "Offline edit");
    store.Write(SharedTable::kItems, "a", std::move(title));
    store.set_loaded();
  }
  SharedStore store(&prefs_);
  EXPECT_EQ("user-1", store.user_id());
  EXPECT_TRUE(store.loaded());
  ASSERT_TRUE(store.Find(SharedTable::kSpaces, "s1"));
  EXPECT_EQ("Work",
            *store.Find(SharedTable::kSpaces, "s1")->columns.FindString("name"));
  EXPECT_EQ("local-1", store.ToLocal(SharedTable::kSpaces, "s1"));
  std::map<std::string, base::DictValue> pending =
      Pending(store, SharedTable::kItems);
  EXPECT_EQ("Offline edit", *pending["a"].FindString("title"));
  ASSERT_TRUE(store.cursor());

  store.Reset("user-2");
  EXPECT_FALSE(store.Find(SharedTable::kSpaces, "s1"));
  EXPECT_FALSE(store.loaded());
}

TEST_F(AvoraSharedStoreTest, ItemsInSortsByPosition) {
  SharedStore store(&prefs_);
  store.MergeFromServer(SharedTable::kItems, ServerRows(R"([
      {"id": "b", "space_id": "s1", "kind": "today", "url": "https://b/",
       "position": "a1", "updated_at": "2026-10-28T16:00:00+00:00",
       "deleted_at": null},
      {"id": "a", "space_id": "s1", "kind": "today", "url": "https://a/",
       "position": "a0", "updated_at": "2026-10-28T16:00:00+00:00",
       "deleted_at": null},
      {"id": "c", "space_id": "s1", "kind": "today", "url": "https://c/",
       "position": "Zz", "updated_at": "2026-10-28T16:00:00+00:00",
       "deleted_at": "2026-10-28T16:00:00+00:00"},
      {"id": "d", "space_id": "s2", "kind": "today", "url": "https://d/",
       "position": "a0", "updated_at": "2026-10-28T16:00:00+00:00",
       "deleted_at": null}])"));
  std::vector<const SharedRow*> rows = store.ItemsIn("s1", kKindToday);
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ("a", rows[0]->id);
  EXPECT_EQ("b", rows[1]->id);
}

}  // namespace
}  // namespace avora::sync
