// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/html_bookmark_parser.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora {

class HtmlBookmarkParserTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath WriteHtml(const std::string& content) {
    base::FilePath path = temp_dir_.GetPath().AppendASCII("bookmarks.html");
    base::WriteFile(path, content);
    return path;
  }

  base::ScopedTempDir temp_dir_;
};

// ─── Basic parsing ──────────────────────────────────────────────────────────

TEST_F(HtmlBookmarkParserTest, EmptyFile) {
  auto result = HtmlBookmarkParser::ParseBookmarksHtml("", "safari", "Export");
  EXPECT_FALSE(result.has_value());
}

TEST_F(HtmlBookmarkParserTest, NotABookmarkFile) {
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(
      "<html><body>Hello world</body></html>", "safari", "Export");
  EXPECT_FALSE(result.has_value());
}

TEST_F(HtmlBookmarkParserTest, MinimalBookmarkFile) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks</H1>
<DL><p>
  <DT><A HREF="https://example.com">Example</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Export");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("safari", result->source.browser);
  EXPECT_EQ("Export", result->source.profile_name);
  EXPECT_EQ(1u, result->source.items.size());
  EXPECT_EQ("Example", result->source.items[0].title);
  EXPECT_EQ("https://example.com/", result->source.items[0].url);
  EXPECT_EQ(ImportedItemType::kLink, result->source.items[0].type);
  EXPECT_EQ(1, result->stats.links_seen);
  EXPECT_EQ(1, result->stats.links_imported);
  EXPECT_EQ(0, result->stats.items_skipped);
}

TEST_F(HtmlBookmarkParserTest, NestedFolders) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><H3>Work</H3>
  <DL><p>
    <DT><H3>Projects</H3>
    <DL><p>
      <DT><A HREF="https://github.com">GitHub</A>
    </DL>
    <DT><A HREF="https://jira.com">Jira</A>
  </DL>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "chrome", "Personal");
  ASSERT_TRUE(result.has_value());
  // Work folder + Projects folder + GitHub link + Jira link = 4 items
  EXPECT_EQ(4u, result->source.items.size());
  EXPECT_EQ(2, result->stats.folders_seen);
  EXPECT_EQ(2, result->stats.folders_imported);
  EXPECT_EQ(2, result->stats.links_seen);
  EXPECT_EQ(2, result->stats.links_imported);

  // Verify hierarchy: find "Projects" folder and check its parent.
  const ImportedItem* work = nullptr;
  const ImportedItem* projects = nullptr;
  const ImportedItem* github = nullptr;
  const ImportedItem* jira = nullptr;
  for (const auto& item : result->source.items) {
    if (item.title == "Work") work = &item;
    if (item.title == "Projects") projects = &item;
    if (item.title == "GitHub") github = &item;
    if (item.title == "Jira") jira = &item;
  }
  ASSERT_NE(nullptr, work);
  ASSERT_NE(nullptr, projects);
  ASSERT_NE(nullptr, github);
  ASSERT_NE(nullptr, jira);
  EXPECT_EQ(projects->parent_id, work->id);
  EXPECT_EQ(github->parent_id, projects->id);
  EXPECT_EQ(jira->parent_id, work->id);
}

TEST_F(HtmlBookmarkParserTest, InvalidUrlsSkipped) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://valid.com">Valid</A>
  <DT><A HREF="javascript:void(0)">JS Link</A>
  <DT><A HREF="data:text/html,hello">Data</A>
  <DT><A HREF="mailto:test@test.com">Email</A>
  <DT><A HREF="https://also-valid.com">Also Valid</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2, result->stats.links_imported);
  EXPECT_EQ(5, result->stats.links_seen);
  EXPECT_EQ(3, result->stats.items_skipped);
}

TEST_F(HtmlBookmarkParserTest, DuplicateUrlsPreserved) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://example.com">First</A>
  <DT><A HREF="https://example.com">Second</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2u, result->source.items.size());
  EXPECT_EQ(2, result->stats.links_imported);
}

TEST_F(HtmlBookmarkParserTest, HtmlEntitiesUnescaped) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://example.com">Tom &amp; Jerry &lt;3&gt;</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("Tom & Jerry <3>", result->source.items[0].title);
}

TEST_F(HtmlBookmarkParserTest, SiblingOrdering) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://a.com">A</A>
  <DT><A HREF="https://b.com">B</A>
  <DT><A HREF="https://c.com">C</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(3u, result->source.items.size());
  EXPECT_EQ(0, result->source.items[0].order);
  EXPECT_EQ(1, result->source.items[1].order);
  EXPECT_EQ(2, result->source.items[2].order);
}

TEST_F(HtmlBookmarkParserTest, HrSeparatorsSkipped) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://a.com">A</A>
  <HR>
  <DT><A HREF="https://b.com">B</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2, result->stats.links_imported);
  EXPECT_GE(result->stats.items_skipped, 1);
}

// ─── File-based parsing ─────────────────────────────────────────────────────

TEST_F(HtmlBookmarkParserTest, ParseFromFile) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://example.com">From File</A>
</DL>
)";
  base::FilePath path = WriteHtml(html);
  auto result = HtmlBookmarkParser::ParseBookmarksHtmlFile(
      path, "safari", "Exported Bookmarks");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(1, result->stats.links_imported);
  EXPECT_EQ("From File", result->source.items[0].title);
}

TEST_F(HtmlBookmarkParserTest, MissingFile) {
  auto result = HtmlBookmarkParser::ParseBookmarksHtmlFile(
      temp_dir_.GetPath().AppendASCII("nonexistent.html"),
      "safari", "Test");
  EXPECT_FALSE(result.has_value());
}

TEST_F(HtmlBookmarkParserTest, MalformedHtmlGraceful) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://valid.com">Valid</A>
  <DT><A HREF="broken no closing quote>Broken</A>
  <DT><A>No href</A>
  <DT><A HREF="https://also-valid.com">Also Valid</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  // "Valid" and "Also Valid" should import; "Broken" has mangled href;
  // "No href" has empty URL.
  EXPECT_GE(result->stats.links_imported, 2);
}

// ─── Safari export format ───────────────────────────────────────────────────

TEST_F(HtmlBookmarkParserTest, SafariExportFormat) {
  // Realistic Safari "Export Bookmarks" output structure.
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<!-- This is an automatically generated file.
     It will be read and overwritten.
     DO NOT EDIT! -->
<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks</H1>
<DL><p>
    <DT><H3 FOLDED>Favorites</H3>
    <DL><p>
        <DT><A HREF="https://www.apple.com/">Apple</A>
        <DT><A HREF="https://news.ycombinator.com/">Hacker News</A>
    </DL><p>
    <DT><H3 FOLDED>BookmarksMenu</H3>
    <DL><p>
        <DT><H3 FOLDED>Development</H3>
        <DL><p>
            <DT><A HREF="https://github.com/">GitHub</A>
            <DT><A HREF="https://stackoverflow.com/">Stack Overflow</A>
        </DL><p>
        <DT><A HREF="https://www.wikipedia.org/">Wikipedia</A>
    </DL><p>
</DL><p>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(
      html, "safari", "Exported Bookmarks");
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(5, result->stats.links_imported);
  EXPECT_EQ(3, result->stats.folders_imported);  // Favorites, BookmarksMenu, Development
  EXPECT_EQ(0, result->stats.items_skipped);
  EXPECT_EQ(0, result->stats.items_excluded);
}

// ─── Coordinator integration ────────────────────────────────────────────────

TEST_F(HtmlBookmarkParserTest, CoordinatorImportHtmlFile) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://example.com">Example</A>
</DL>
)";
  base::FilePath path = WriteHtml(html);

  // Verify the coordinator can import the file.
  // We can't easily test with ImportedLinkStore without prefs, but
  // we can verify the parser integration path works.
  auto parse_result = HtmlBookmarkParser::ParseBookmarksHtmlFile(
      path, "safari", "Exported Bookmarks");
  ASSERT_TRUE(parse_result.has_value());
  EXPECT_EQ(1, parse_result->stats.links_imported);
  EXPECT_EQ("safari", parse_result->source.browser);
}

TEST_F(HtmlBookmarkParserTest, EmptyFoldersPreserved) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><H3>Empty Folder</H3>
  <DL><p>
  </DL>
  <DT><A HREF="https://example.com">Link</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  // Empty folder is created when <DL> follows <H3>.
  EXPECT_EQ(1, result->stats.folders_imported);
  EXPECT_EQ(1, result->stats.links_imported);
}

TEST_F(HtmlBookmarkParserTest, UnicodePreserved) {
  const std::string html = R"(
<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
  <DT><A HREF="https://example.com">Ünïcödé Títlé 日本語</A>
</DL>
)";
  auto result = HtmlBookmarkParser::ParseBookmarksHtml(html, "safari", "Test");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("Ünïcödé Títlé 日本語",
            result->source.items[0].title);
}

}  // namespace avora
