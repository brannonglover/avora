// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_shared_rows.h"

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora::sync {
namespace {

base::Time Utc(int year, int month, int day, int hour, int minute, int second) {
  base::Time::Exploded exploded = {};
  exploded.year = year;
  exploded.month = month;
  exploded.day_of_month = day;
  exploded.hour = hour;
  exploded.minute = minute;
  exploded.second = second;
  base::Time time;
  EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
  return time;
}

TEST(AvoraSharedRowsTest, ParsesPostgrestTimestamps) {
  const base::Time base = Utc(2026, 10, 28, 16, 5, 1);
  EXPECT_EQ(base, ParseServerTime("2026-10-28T16:05:01+00:00"));
  EXPECT_EQ(base, ParseServerTime("2026-10-28T16:05:01Z"));
  EXPECT_EQ(base + base::Microseconds(120000),
            ParseServerTime("2026-10-28T16:05:01.12+00:00"));
  EXPECT_EQ(base + base::Microseconds(123456),
            ParseServerTime("2026-10-28T16:05:01.123456+00:00"));
  EXPECT_EQ(base, ParseServerTime("2026-10-28T18:05:01+02:00"));
  EXPECT_EQ(base, ParseServerTime("2026-10-28T11:35:01-04:30"));
  EXPECT_EQ(base, ParseServerTime("2026-10-28 16:05:01+00"));

  EXPECT_FALSE(ParseServerTime(""));
  EXPECT_FALSE(ParseServerTime("2026-10-28T16:05:01"));
  EXPECT_FALSE(ParseServerTime("2026-10-28T16:05:01.+00:00"));
  EXPECT_FALSE(ParseServerTime("yesterday"));
}

TEST(AvoraSharedRowsTest, FormatsTimestampsTheServerAccepts) {
  const base::Time time =
      Utc(2026, 10, 28, 16, 5, 1) + base::Microseconds(42);
  EXPECT_EQ("2026-10-28T16:05:01.000042+00:00", FormatServerTime(time));
  EXPECT_EQ(time, ParseServerTime(FormatServerTime(time)));
}

TEST(AvoraSharedRowsTest, ReadsAServerRow) {
  std::optional<base::DictValue> json = base::JSONReader::ReadDict(R"({
      "id": "6f1c1d36-8c4c-4d55-9b0c-5d1c1e0b1a01",
      "user_id": "u", "space_id": "s1", "kind": "today",
      "parent_id": null, "url": "https://example.com/", "title": "Example",
      "favicon_url": null, "preview_path": null, "preview_updated_at": null,
      "position": "a0", "last_active_at": null,
      "created_at": "2026-10-28T16:00:00+00:00",
      "updated_at": "2026-10-28T16:05:01.5+00:00",
      "updated_by": "d1", "deleted_at": null})",
                                                                   0);
  ASSERT_TRUE(json);
  std::optional<SharedRow> row = SharedRow::FromServer(SharedTable::kItems, *json);
  ASSERT_TRUE(row);
  EXPECT_EQ("6f1c1d36-8c4c-4d55-9b0c-5d1c1e0b1a01", row->id);
  EXPECT_EQ("d1", row->updated_by);
  EXPECT_FALSE(row->deleted);
  EXPECT_EQ("https://example.com/", *row->columns.FindString("url"));
  EXPECT_TRUE(row->columns.Find("parent_id")->is_none());
  // Bookkeeping columns are not content.
  EXPECT_FALSE(row->columns.Find("user_id"));
  EXPECT_FALSE(row->columns.Find("created_at"));

  json->Set("deleted_at", "2026-10-28T16:06:00+00:00");
  EXPECT_TRUE(SharedRow::FromServer(SharedTable::kItems, *json)->deleted);

  std::optional<SharedRow> round_trip = SharedRow::FromDict(row->ToDict());
  ASSERT_TRUE(round_trip);
  EXPECT_EQ(row->columns, round_trip->columns);
  EXPECT_EQ(row->updated_at, round_trip->updated_at);

  json->Remove("updated_at");
  EXPECT_FALSE(SharedRow::FromServer(SharedTable::kItems, *json));
}

}  // namespace
}  // namespace avora::sync
