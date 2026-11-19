// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_CLEAR_SITE_DATA_H_
#define CHROME_BROWSER_AVORA_AVORA_CLEAR_SITE_DATA_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback_forward.h"

class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace avora {

// Commands typed into the quick-nav overlay ("Clear Cache", "Clear Cookies").
// Each acts on the site of one tab, in that tab's Space: Spaces keep separate
// cookie jars and caches, so clearing a site in one leaves the others alone.
enum class SiteDataCommand {
  kClearCache,
  kClearCookies,
};

// Fewer characters than this show no command rows, so ordinary typing ("cl"
// on the way to a URL) never puts them in the list.
inline constexpr size_t kMinCommandPrefixLength = 5;

// The command's display name, which is also what the user types to run it.
std::u16string SiteDataCommandName(SiteDataCommand command);

// The command |text| names in full, ignoring case and surrounding whitespace.
std::optional<SiteDataCommand> MatchSiteDataCommand(std::u16string_view text);

// The commands whose name begins with |text|, ignoring case and leading
// whitespace, in display order.  Empty for text shorter than
// kMinCommandPrefixLength.
std::vector<SiteDataCommand> SiteDataCommandsStartingWith(
    std::u16string_view text);

// The site a command clears for |url|: its registrable domain ("github.com"
// for "gist.github.com"), or its host where it has none (localhost, an IP).
// Empty for anything that is not http(s), which has no site data to clear.
std::string ClearableSiteForURL(const GURL& url);

// Clears |command|'s data for the site |contents| is showing, from
// |contents|' storage partition only.  |done| runs with whether everything
// was removed.  Nothing is cleared, and |done| runs with false, when the tab
// is not showing an http(s) page.
void ClearSiteData(content::WebContents* contents,
                   SiteDataCommand command,
                   base::OnceCallback<void(bool success)> done);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_CLEAR_SITE_DATA_H_
