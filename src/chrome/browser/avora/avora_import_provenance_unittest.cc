// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_import_provenance.h"

#include <memory>
#include <string>

#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

ImportProvenance MakeProvenance(const std::string& record_type,
                                const std::string& record_id,
                                const std::string& import_source,
                                const std::string& external_id,
                                const std::string& import_batch_id = "") {
  ImportProvenance provenance;
  provenance.record_type = record_type;
  provenance.record_id = record_id;
  provenance.import_source = import_source;
  provenance.external_id = external_id;
  provenance.import_batch_id = import_batch_id;
  return provenance;
}

// Observer that counts notifications.
class TestObserver : public ImportProvenanceStore::Observer {
 public:
  void OnProvenanceChanged() override { ++change_count_; }
  int change_count() const { return change_count_; }
  void Reset() { change_count_ = 0; }

 private:
  int change_count_ = 0;
};

class ImportProvenanceStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ImportProvenanceStore::RegisterProfilePrefs(prefs_.registry());
    store_ = std::make_unique<ImportProvenanceStore>(&prefs_);
    store_->AddObserver(&observer_);
  }

  void TearDown() override { store_->RemoveObserver(&observer_); }

  std::unique_ptr<ImportProvenanceStore> CreateSecondStore() {
    return std::make_unique<ImportProvenanceStore>(&prefs_);
  }

  void SimulateRestart() {
    store_->RemoveObserver(&observer_);
    store_.reset();
    store_ = std::make_unique<ImportProvenanceStore>(&prefs_);
    store_->AddObserver(&observer_);
  }

  TestingPrefServiceSimple prefs_;
  std::unique_ptr<ImportProvenanceStore> store_;
  TestObserver observer_;
};

// ── Basic set / get ─────────────────────────────────────────────────────────

TEST_F(ImportProvenanceStoreTest, EmptyStoreReturnsNothing) {
  EXPECT_THAT(store_->GetAll(), IsEmpty());
  EXPECT_EQ(store_->GetByRecord("space", "space1"), nullptr);
  EXPECT_EQ(store_->GetByExternalId("arc", "space", "arc-uuid-1"), nullptr);
}

TEST_F(ImportProvenanceStoreTest, SetAndRetrieveByRecord) {
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1",
                            "batch-1"));

  const ImportProvenance* found = store_->GetByRecord("space", "avora-space-1");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->import_source, "arc");
  EXPECT_EQ(found->external_id, "arc-space-1");
  EXPECT_EQ(found->import_batch_id, "batch-1");
}

TEST_F(ImportProvenanceStoreTest, SetAndRetrieveByExternalId) {
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1"));

  const ImportProvenance* found =
      store_->GetByExternalId("arc", "space", "arc-space-1");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->record_id, "avora-space-1");
}

TEST_F(ImportProvenanceStoreTest, GetByExternalIdScopedToSourceAndType) {
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "shared-id"));

  // Same external_id, different record_type -- must not match.
  EXPECT_EQ(store_->GetByExternalId("arc", "pinned_item", "shared-id"),
           nullptr);
  // Same external_id, different import_source -- must not match.
  EXPECT_EQ(store_->GetByExternalId("chrome", "space", "shared-id"), nullptr);
}

// ── Set replaces existing for same (record_type, record_id) ────────────────

TEST_F(ImportProvenanceStoreTest, SetReplacesExistingForSameRecord) {
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-v1"));
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-v2"));

  EXPECT_THAT(store_->GetAll(), SizeIs(1));
  const ImportProvenance* found = store_->GetByRecord("space", "avora-space-1");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->external_id, "arc-v2");
}

TEST_F(ImportProvenanceStoreTest, DifferentRecordTypesWithSameIdCoexist) {
  // A Space and a pinned item could plausibly reuse the same underlying id
  // scheme; record_type keeps them distinct.
  store_->Set(MakeProvenance("space", "same-id", "arc", "arc-a"));
  store_->Set(MakeProvenance("pinned_item", "same-id", "arc", "arc-b"));

  EXPECT_THAT(store_->GetAll(), SizeIs(2));
  EXPECT_EQ(store_->GetByRecord("space", "same-id")->external_id, "arc-a");
  EXPECT_EQ(store_->GetByRecord("pinned_item", "same-id")->external_id,
           "arc-b");
}

// ── Remove ───────────────────────────────────────────────────────────────────

TEST_F(ImportProvenanceStoreTest, RemoveByRecordLeavesOthersIntact) {
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1"));
  store_->Set(MakeProvenance("space", "avora-space-2", "arc", "arc-space-2"));

  store_->RemoveByRecord("space", "avora-space-1");

  EXPECT_THAT(store_->GetAll(), SizeIs(1));
  EXPECT_EQ(store_->GetByRecord("space", "avora-space-1"), nullptr);
  EXPECT_NE(store_->GetByRecord("space", "avora-space-2"), nullptr);
}

TEST_F(ImportProvenanceStoreTest, RemoveByRecordNonexistentIsNoOp) {
  observer_.Reset();
  store_->RemoveByRecord("space", "nonexistent");
  EXPECT_EQ(observer_.change_count(), 0);
}

// ── Persistence / reload ────────────────────────────────────────────────────

TEST_F(ImportProvenanceStoreTest, SurvivesRestart) {
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1",
                            "batch-1"));
  SimulateRestart();

  const ImportProvenance* found = store_->GetByRecord("space", "avora-space-1");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->import_source, "arc");
  EXPECT_EQ(found->external_id, "arc-space-1");
  EXPECT_EQ(found->import_batch_id, "batch-1");
}

// ── Serialization round-trip ────────────────────────────────────────────────

TEST_F(ImportProvenanceStoreTest, SerializationRoundTrip) {
  ImportProvenance original =
      MakeProvenance("favorite_batch", "space-abc", "arc", "arc-profile-1",
                    "batch-xyz");

  base::DictValue dict = original.ToDict();
  ImportProvenance restored = ImportProvenance::FromDict(dict);

  EXPECT_EQ(restored.record_type, original.record_type);
  EXPECT_EQ(restored.record_id, original.record_id);
  EXPECT_EQ(restored.import_source, original.import_source);
  EXPECT_EQ(restored.external_id, original.external_id);
  EXPECT_EQ(restored.import_batch_id, original.import_batch_id);
}

// ── Observer notifications ──────────────────────────────────────────────────

TEST_F(ImportProvenanceStoreTest, ObserverNotifiedOnSet) {
  observer_.Reset();
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1"));
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(ImportProvenanceStoreTest, ObserverNotifiedOnRemove) {
  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1"));
  observer_.Reset();
  store_->RemoveByRecord("space", "avora-space-1");
  EXPECT_EQ(observer_.change_count(), 1);
}

TEST_F(ImportProvenanceStoreTest, CrossInstanceSync) {
  auto second_store = CreateSecondStore();
  TestObserver second_observer;
  second_store->AddObserver(&second_observer);

  store_->Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1"));

  EXPECT_EQ(second_observer.change_count(), 1);
  EXPECT_THAT(second_store->GetAll(), SizeIs(1));

  second_store->RemoveObserver(&second_observer);
}

// ── No prefs registered ─────────────────────────────────────────────────────

// Without a registered backing pref, the store (like every other store in
// this codebase, e.g. ImportedLinkStore) still updates its in-memory cache
// on a write -- there is nowhere to persist to, but nothing crashes, and the
// write is visible for the lifetime of that one instance. What does NOT
// happen is persistence: a second instance created without a registered
// pref sees none of the first instance's writes.
TEST(ImportProvenanceStoreNoPrefTest, WorksWithoutRegisteredPrefs) {
  TestingPrefServiceSimple prefs;
  // Deliberately do NOT register prefs.
  ImportProvenanceStore store(&prefs);

  EXPECT_THAT(store.GetAll(), IsEmpty());

  store.Set(MakeProvenance("space", "avora-space-1", "arc", "arc-space-1"));
  EXPECT_THAT(store.GetAll(), SizeIs(1));

  // A second instance over the same (still-unregistered) PrefService has no
  // way to see the first instance's in-memory-only write.
  ImportProvenanceStore second_store(&prefs);
  EXPECT_THAT(second_store.GetAll(), IsEmpty());

  store.RemoveByRecord("space", "nonexistent");
}

}  // namespace
}  // namespace avora
