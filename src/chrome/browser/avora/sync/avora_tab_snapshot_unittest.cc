// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_tab_snapshot.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace avora::sync {
namespace {

OpenTab Tab(std::string_view url,
            std::string_view title = "Title",
            std::string guid = std::string()) {
  OpenTab tab;
  tab.url = GURL(url);
  tab.title = base::UTF8ToUTF16(title);
  tab.guid = std::move(guid);
  return tab;
}

std::vector<std::string> Urls(const base::ListValue& list) {
  std::vector<std::string> urls;
  for (const base::Value& value : list) {
    urls.push_back(*value.GetDict().FindString("url"));
  }
  return urls;
}

TEST(AvoraTabSnapshotTest, OnlyHttpAndHttpsUrlsAreSyncable) {
  EXPECT_TRUE(IsSyncableUrl(GURL("https://example.com/a?b#c")));
  EXPECT_TRUE(IsSyncableUrl(GURL("http://localhost:8080/")));
  EXPECT_FALSE(IsSyncableUrl(GURL("chrome://newtab/")));
  EXPECT_FALSE(IsSyncableUrl(GURL("chrome://avora-settings")));
  EXPECT_FALSE(IsSyncableUrl(GURL("file:///Users/me/notes.txt")));
  EXPECT_FALSE(IsSyncableUrl(GURL("about:blank")));
  EXPECT_FALSE(IsSyncableUrl(GURL("data:text/html,hi")));
  EXPECT_FALSE(IsSyncableUrl(GURL("javascript:alert(1)")));
  EXPECT_FALSE(IsSyncableUrl(GURL("chrome-extension://abc/popup.html")));
  EXPECT_FALSE(IsSyncableUrl(GURL()));
  EXPECT_FALSE(IsSyncableUrl(GURL("not a url")));
}

TEST(AvoraTabSnapshotTest, LeavesOutLocalPagesAndKeepsOrder) {
  base::ListValue list = BuildTabsList({
      Tab("https://a.example/"),
      Tab("chrome://newtab/"),
      Tab("http://b.example/page"),
      Tab("file:///tmp/x.html"),
      Tab("about:blank"),
      Tab("https://c.example/"),
  });
  EXPECT_EQ(Urls(list),
            (std::vector<std::string>{"https://a.example/",
                                      "http://b.example/page",
                                      "https://c.example/"}));
}

TEST(AvoraTabSnapshotTest, WritesEveryFieldOfTheContract) {
  OpenTab tab = Tab("https://example.com/page", "Example page");
  tab.pinned = true;
  tab.space = "Work";
  tab.last_active = base::Time::FromMillisecondsSinceUnixEpoch(1793000000000);

  base::ListValue list = BuildTabsList({tab});
  ASSERT_EQ(list.size(), 1u);
  const base::DictValue& dict = list[0].GetDict();
  EXPECT_EQ(dict.size(), 5u);
  EXPECT_EQ(*dict.FindString("url"), "https://example.com/page");
  EXPECT_EQ(*dict.FindString("title"), "Example page");
  EXPECT_EQ(dict.FindBool("pinned"), true);
  EXPECT_EQ(*dict.FindString("space"), "Work");
  EXPECT_EQ(dict.FindDouble("lastActiveAt"), 1793000000000.0);
}

TEST(AvoraTabSnapshotTest, OptionalFieldsAreLeftOutAndTitleFallsBackToUrl) {
  base::ListValue list = BuildTabsList({Tab("https://example.com/", "")});
  ASSERT_EQ(list.size(), 1u);
  const base::DictValue& dict = list[0].GetDict();
  EXPECT_EQ(*dict.FindString("title"), "https://example.com/");
  EXPECT_EQ(dict.FindBool("pinned"), false);
  EXPECT_FALSE(dict.Find("space"));
  EXPECT_FALSE(dict.Find("lastActiveAt"));
}

TEST(AvoraTabSnapshotTest, FoldsCopiesOfOneTabAcrossWindows) {
  // The same daily tab, copied into three windows. The copy in window 2 is
  // the one the user last looked at, so it supplies the page; window 3's copy
  // is pinned there.
  OpenTab stale = Tab("https://old.example/", "Old", "guid-1");
  stale.last_active = base::Time::FromMillisecondsSinceUnixEpoch(1000);
  OpenTab fresh = Tab("https://new.example/", "New", "guid-1");
  fresh.last_active = base::Time::FromMillisecondsSinceUnixEpoch(3000);
  OpenTab pinned_copy = Tab("https://old.example/", "Old", "guid-1");
  pinned_copy.pinned = true;
  pinned_copy.last_active = base::Time::FromMillisecondsSinceUnixEpoch(2000);

  base::ListValue list = BuildTabsList({
      stale,
      Tab("https://other.example/", "Other", "guid-2"),
      fresh,
      pinned_copy,
  });
  ASSERT_EQ(list.size(), 2u);
  const base::DictValue& merged = list[0].GetDict();
  EXPECT_EQ(*merged.FindString("url"), "https://new.example/");
  EXPECT_EQ(*merged.FindString("title"), "New");
  EXPECT_EQ(merged.FindBool("pinned"), true);
  EXPECT_EQ(merged.FindDouble("lastActiveAt"), 3000.0);
  EXPECT_EQ(*list[1].GetDict().FindString("url"), "https://other.example/");
}

TEST(AvoraTabSnapshotTest, CredentialsInTheUrlAreNotSent) {
  base::ListValue list =
      BuildTabsList({Tab("https://user:secret@example.com/path?q=1#top", "")});
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(*list[0].GetDict().FindString("url"),
            "https://example.com/path?q=1#top");
  EXPECT_EQ(*list[0].GetDict().FindString("title"),
            "https://example.com/path?q=1#top");
}

TEST(AvoraTabSnapshotTest, TabsWithoutGuidAreNotFolded) {
  base::ListValue list = BuildTabsList(
      {Tab("https://same.example/"), Tab("https://same.example/")});
  EXPECT_EQ(list.size(), 2u);
}

TEST(AvoraTabSnapshotTest, UpsertBodyMatchesTheContract) {
  OpenTab tab = Tab("https://example.com/", "Example");
  tab.last_active = base::Time::FromMillisecondsSinceUnixEpoch(1793000000000);
  const std::string body = BuildDeviceUpsertBody(
      "6f1c1f5e-2b5d-4f0e-9a51-0d6c1b0f7c11", "Brannon's MacBook Pro", "macos",
      BuildTabsList({tab}));

  // Whole milliseconds, not 1793000000000.0.
  EXPECT_NE(body.find("\"lastActiveAt\":1793000000000,"), std::string::npos)
      << body;

  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(dict);
  EXPECT_EQ(dict->size(), 4u);
  EXPECT_EQ(*dict->FindString("id"), "6f1c1f5e-2b5d-4f0e-9a51-0d6c1b0f7c11");
  EXPECT_EQ(*dict->FindString("name"), "Brannon's MacBook Pro");
  EXPECT_EQ(*dict->FindString("platform"), "macos");
  const base::ListValue* tabs = dict->FindList("tabs");
  ASSERT_TRUE(tabs);
  ASSERT_EQ(tabs->size(), 1u);
  EXPECT_EQ(*(*tabs)[0].GetDict().FindString("url"), "https://example.com/");
}

TEST(AvoraTabSnapshotTest, EmptySnapshotIsAnEmptyArray) {
  const std::string body =
      BuildDeviceUpsertBody("id", "Mac", "macos",
                            BuildTabsList({Tab("chrome://newtab/")}));
  EXPECT_NE(body.find("\"tabs\":[]"), std::string::npos) << body;
}

#if BUILDFLAG(IS_MAC)
TEST(AvoraTabSnapshotTest, PlatformIsMacos) {
  EXPECT_STREQ(CurrentPlatform(), "macos");
}
#endif

}  // namespace
}  // namespace avora::sync
