// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_tab_snapshot.h"

#include <map>
#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"

namespace avora::sync {

namespace {

constexpr char kUrlKey[] = "url";
constexpr char kTitleKey[] = "title";
constexpr char kPinnedKey[] = "pinned";
constexpr char kSpaceKey[] = "space";
constexpr char kLastActiveAtKey[] = "lastActiveAt";

base::DictValue ToDict(const OpenTab& tab) {
  base::DictValue dict;
  // A username or password in the URL stays on this device.
  GURL::Replacements strip_credentials;
  strip_credentials.ClearUsername();
  strip_credentials.ClearPassword();
  const std::string spec = tab.url.ReplaceComponents(strip_credentials).spec();
  dict.Set(kUrlKey, spec);
  // `title` is required; a page that has not reported one yet shows its URL,
  // as the tab strip does.
  dict.Set(kTitleKey,
           tab.title.empty() ? spec : base::UTF16ToUTF8(tab.title));
  dict.Set(kPinnedKey, tab.pinned);
  if (!tab.space.empty()) {
    dict.Set(kSpaceKey, tab.space);
  }
  if (!tab.last_active.is_null()) {
    // base::Value has no int64, but a double holds every millisecond
    // timestamp exactly, and BuildDeviceUpsertBody() writes whole doubles
    // without a fraction.
    dict.Set(kLastActiveAtKey,
             static_cast<double>(
                 tab.last_active.InMillisecondsSinceUnixEpoch()));
  }
  return dict;
}

}  // namespace

bool IsSyncableUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && url.has_host();
}

base::ListValue BuildTabsList(const std::vector<OpenTab>& tabs) {
  std::vector<OpenTab> merged;
  // GUID -> index into `merged`.
  std::map<std::string, size_t> by_guid;
  for (const OpenTab& tab : tabs) {
    if (!IsSyncableUrl(tab.url)) {
      continue;
    }
    if (tab.guid.empty()) {
      merged.push_back(tab);
      continue;
    }
    auto [it, inserted] = by_guid.emplace(tab.guid, merged.size());
    if (inserted) {
      merged.push_back(tab);
      continue;
    }
    OpenTab& existing = merged[it->second];
    const bool pinned = existing.pinned || tab.pinned;
    if (tab.last_active > existing.last_active) {
      existing = tab;
    }
    existing.pinned = pinned;
  }

  base::ListValue list;
  for (const OpenTab& tab : merged) {
    list.Append(ToDict(tab));
  }
  return list;
}

std::string BuildDeviceUpsertBody(std::string_view device_id,
                                  std::string_view device_name,
                                  std::string_view platform,
                                  base::ListValue tabs) {
  base::DictValue body;
  body.Set("id", device_id);
  body.Set("name", device_name);
  body.Set("platform", platform);
  body.Set("tabs", std::move(tabs));
  // Without OMIT_DOUBLE_TYPE_PRESERVATION, lastActiveAt would be written as
  // 1793000000000.0.
  return base::WriteJsonWithOptions(
             body, base::JSONWriter::OPTIONS_OMIT_DOUBLE_TYPE_PRESERVATION)
      .value_or(std::string());
}

const char* CurrentPlatform() {
#if BUILDFLAG(IS_MAC)
  return "macos";
#elif BUILDFLAG(IS_WIN)
  return "windows";
#else
  return "linux";
#endif
}

}  // namespace avora::sync
