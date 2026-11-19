// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_clear_site_data.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace avora {
namespace {

TEST(AvoraClearSiteDataTest, MatchesFullNameIgnoringCaseAndSpace) {
  EXPECT_EQ(MatchSiteDataCommand(u"Clear Cache"), SiteDataCommand::kClearCache);
  EXPECT_EQ(MatchSiteDataCommand(u"clear cache"), SiteDataCommand::kClearCache);
  EXPECT_EQ(MatchSiteDataCommand(u"  CLEAR COOKIES "),
            SiteDataCommand::kClearCookies);
}

TEST(AvoraClearSiteDataTest, DoesNotMatchPartialOrLongerText) {
  EXPECT_EQ(MatchSiteDataCommand(u"clear"), std::nullopt);
  EXPECT_EQ(MatchSiteDataCommand(u"clear cach"), std::nullopt);
  EXPECT_EQ(MatchSiteDataCommand(u"clear cache chrome"), std::nullopt);
  EXPECT_EQ(MatchSiteDataCommand(u"clearcache"), std::nullopt);
  EXPECT_EQ(MatchSiteDataCommand(u""), std::nullopt);
}

TEST(AvoraClearSiteDataTest, PrefixNeedsEnoughCharacters) {
  EXPECT_TRUE(SiteDataCommandsStartingWith(u"c").empty());
  EXPECT_TRUE(SiteDataCommandsStartingWith(u"clea").empty());
  EXPECT_TRUE(SiteDataCommandsStartingWith(u"   clea").empty());
}

TEST(AvoraClearSiteDataTest, PrefixListsCommandsInOrder) {
  EXPECT_EQ(SiteDataCommandsStartingWith(u"Clear"),
            (std::vector<SiteDataCommand>{SiteDataCommand::kClearCache,
                                          SiteDataCommand::kClearCookies}));
  EXPECT_EQ(SiteDataCommandsStartingWith(u"clear c"),
            (std::vector<SiteDataCommand>{SiteDataCommand::kClearCache,
                                          SiteDataCommand::kClearCookies}));
  EXPECT_EQ(SiteDataCommandsStartingWith(u"clear co"),
            std::vector<SiteDataCommand>{SiteDataCommand::kClearCookies});
  EXPECT_EQ(SiteDataCommandsStartingWith(u"CLEAR CACHE"),
            std::vector<SiteDataCommand>{SiteDataCommand::kClearCache});
  EXPECT_TRUE(SiteDataCommandsStartingWith(u"clear cache now").empty());
  EXPECT_TRUE(SiteDataCommandsStartingWith(u"cleveland").empty());
}

TEST(AvoraClearSiteDataTest, SiteIsRegistrableDomain) {
  EXPECT_EQ(ClearableSiteForURL(GURL("https://gist.github.com/a/b")),
            "github.com");
  EXPECT_EQ(ClearableSiteForURL(GURL("http://www.bbc.co.uk/")), "bbc.co.uk");
}

TEST(AvoraClearSiteDataTest, SiteFallsBackToHost) {
  EXPECT_EQ(ClearableSiteForURL(GURL("http://localhost:3000/")), "localhost");
  EXPECT_EQ(ClearableSiteForURL(GURL("http://127.0.0.1:8080/")), "127.0.0.1");
}

TEST(AvoraClearSiteDataTest, NonWebPagesHaveNoSite) {
  EXPECT_EQ(ClearableSiteForURL(GURL("chrome://newtab/")), "");
  EXPECT_EQ(ClearableSiteForURL(GURL("about:blank")), "");
  EXPECT_EQ(ClearableSiteForURL(GURL("file:///tmp/a.html")), "");
  EXPECT_EQ(ClearableSiteForURL(GURL()), "");
}

}  // namespace
}  // namespace avora
