// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/import/html_bookmark_parser.h"

#include <stack>
#include <utility>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr size_t kMaxFileSize = 64 * 1024 * 1024;  // 64 MB

std::string GenerateId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

// Simple HTML entity unescaping for bookmark titles.
std::string UnescapeHtml(const std::string& text) {
  std::string result = text;
  base::ReplaceSubstringsAfterOffset(&result, 0, "&amp;", "&");
  base::ReplaceSubstringsAfterOffset(&result, 0, "&lt;", "<");
  base::ReplaceSubstringsAfterOffset(&result, 0, "&gt;", ">");
  base::ReplaceSubstringsAfterOffset(&result, 0, "&quot;", "\"");
  base::ReplaceSubstringsAfterOffset(&result, 0, "&#39;", "'");
  base::ReplaceSubstringsAfterOffset(&result, 0, "&apos;", "'");
  return result;
}

// Extract the value of an attribute from an HTML tag.
// e.g. GetAttr(R"(<A HREF="http://example.com" ADD_DATE="123">)", "HREF")
//   => "http://example.com"
std::string GetAttr(const std::string& tag, const std::string& attr) {
  // Case-insensitive search for the attribute name.
  std::string lower_tag = base::ToLowerASCII(tag);
  std::string lower_attr = base::ToLowerASCII(attr) + "=\"";
  size_t pos = lower_tag.find(lower_attr);
  if (pos == std::string::npos) return {};

  size_t start = pos + lower_attr.size();
  size_t end = tag.find('"', start);
  if (end == std::string::npos) return {};

  return tag.substr(start, end - start);
}

// Extract text between > and < in a tag line.
// e.g. ExtractText("<A ...>My Title</A>") => "My Title"
std::string ExtractText(const std::string& line,
                        const std::string& close_tag) {
  size_t gt = line.find('>');
  if (gt == std::string::npos) return {};
  std::string lower = base::ToLowerASCII(line);
  std::string lower_close = base::ToLowerASCII(close_tag);
  size_t end = lower.find(lower_close, gt);
  if (end == std::string::npos) {
    end = line.size();
  }
  return UnescapeHtml(line.substr(gt + 1, end - gt - 1));
}

bool IsValidImportUrl(const GURL& url) {
  if (!url.is_valid()) return false;
  std::string_view scheme = url.scheme();
  return scheme == "http" || scheme == "https" || scheme == "ftp" ||
         scheme == "file" || scheme == "chrome";
}

// Represents a folder being built during parsing.
struct FolderContext {
  std::string id;
  std::string title;
  std::string parent_id;
  int order = 0;
};

}  // namespace

// static
std::optional<ParseResult> HtmlBookmarkParser::ParseBookmarksHtmlFile(
    const base::FilePath& html_path,
    const std::string& browser,
    const std::string& profile_name) {
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(html_path, &contents, kMaxFileSize)) {
    LOG(WARNING) << "Failed to read bookmark HTML file: " << html_path;
    return std::nullopt;
  }
  return ParseBookmarksHtml(contents, browser, profile_name);
}

// static
std::optional<ParseResult> HtmlBookmarkParser::ParseBookmarksHtml(
    const std::string& html,
    const std::string& browser,
    const std::string& profile_name) {
  ParseResult result;
  result.source.browser = browser;
  result.source.profile_name = profile_name;
  result.source.imported_at = base::Time::Now();

  // Parse state: stack of folder contexts tracking the current position
  // in the bookmark hierarchy.
  std::stack<FolderContext> folder_stack;

  // Root (empty parent_id) starts with a virtual root context.
  folder_stack.push({"", "", "", 0});

  // Pending folder: when we see <H3>title</H3>, we note it but don't
  // create the folder until the subsequent <DL> confirms it has children.
  std::string pending_folder_title;
  bool has_pending_folder = false;

  std::vector<std::string> lines = base::SplitString(
      html, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  for (std::string& line : lines) {
    base::TrimString(line, " \t\r", &line);

    // Remove <DT> prefix.
    if (base::StartsWith(line, "<DT>",
                         base::CompareCase::INSENSITIVE_ASCII) ||
        base::StartsWith(line, "<dt>",
                         base::CompareCase::INSENSITIVE_ASCII)) {
      line = line.substr(4);
      base::TrimString(line, " \t", &line);
    }

    // Remove leading <HR> separators (Firefox).
    while (base::StartsWith(line, "<HR>",
                            base::CompareCase::INSENSITIVE_ASCII) ||
           base::StartsWith(line, "<hr>",
                            base::CompareCase::INSENSITIVE_ASCII)) {
      line = line.substr(4);
      base::TrimString(line, " \t", &line);
      result.stats.items_skipped++;
    }

    // Folder header: <H3 ...>Title</H3>
    std::string lower = base::ToLowerASCII(line);
    if (base::StartsWith(lower, "<h3")) {
      pending_folder_title = ExtractText(line, "</H3>");
      has_pending_folder = true;
      result.stats.folders_seen++;
      continue;
    }

    // Bookmark link: <A HREF="...">Title</A>
    if (base::StartsWith(lower, "<a ")) {
      result.stats.links_seen++;

      std::string href = GetAttr(line, "HREF");
      GURL url(href);
      if (!IsValidImportUrl(url)) {
        result.stats.items_skipped++;
        continue;
      }

      std::string title = ExtractText(line, "</A>");
      if (title.empty()) title = href;

      ImportedItem item;
      item.id = GenerateId();
      item.type = ImportedItemType::kLink;
      item.title = title;
      item.url = url.spec();
      item.parent_id = folder_stack.top().id;
      item.order = folder_stack.top().order++;

      result.source.items.push_back(std::move(item));
      result.stats.links_imported++;
      continue;
    }

    // Start of a folder's content: <DL>
    if (base::StartsWith(lower, "<dl")) {
      if (has_pending_folder) {
        // Create the folder.
        std::string folder_id = GenerateId();
        ImportedItem folder;
        folder.id = folder_id;
        folder.type = ImportedItemType::kFolder;
        folder.title = pending_folder_title;
        folder.parent_id = folder_stack.top().id;
        folder.order = folder_stack.top().order++;

        result.source.items.push_back(std::move(folder));
        result.stats.folders_imported++;

        folder_stack.push(
            {folder_id, pending_folder_title,
             folder_stack.top().id, 0});
        has_pending_folder = false;
        pending_folder_title.clear();
      } else {
        // Top-level <DL> or unexpected nesting — push a transparent context.
        folder_stack.push(folder_stack.top());
        folder_stack.top().order = 0;
      }
      continue;
    }

    // End of a folder: </DL>
    if (base::StartsWith(lower, "</dl")) {
      if (folder_stack.size() > 1) {
        folder_stack.pop();
      }
      continue;
    }
  }

  // If we got no bookmarks at all, the file wasn't a valid bookmark file.
  if (result.source.items.empty() &&
      result.stats.links_seen == 0 &&
      result.stats.folders_seen == 0) {
    return std::nullopt;
  }

  return result;
}

}  // namespace avora
