// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_OMNIBOX_BRIDGE_H_
#define CHROME_BROWSER_AVORA_AVORA_OMNIBOX_BRIDGE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "url/gurl.h"

class Profile;

namespace avora {

// A single ranked suggestion, flattened out of AutocompleteMatch into the few
// fields Avora's overlay actually draws.  Chromium decides what goes in this
// list and in what order; everything about how it looks is decided by
// AvoraQuickNavView.
struct OmniboxSuggestion {
  // What to hand the navigation callback when this row is chosen.  For URL
  // matches this is the resolved destination.  For search matches it is the
  // raw query text rather than Chromium's search URL, so that Avora's own
  // SearchEngineManager picks which engine actually runs the query.
  std::string commit_value;

  // Destination of a URL match, used to look up a favicon.  Empty for search
  // matches, which draw the search glyph instead.
  GURL destination_url;

  // Leading line ("Gmail") and trailing line ("mail.google.com").  Search
  // matches carry only a title.
  std::u16string title;
  std::u16string subtitle;

  bool is_search = false;
};

struct OmniboxResults {
  // The text these results were produced for.  Results arrive asynchronously,
  // so the view compares this against the field before drawing anything.
  std::u16string query;

  std::vector<OmniboxSuggestion> suggestions;

  // Arc-style type-ahead: `query + inline_completion` is the text to place in
  // the field, with the completion portion selected.  Empty when the
  // top-ranked match cannot be the default, or when the caller suppressed
  // inline autocompletion.
  std::u16string inline_completion;

  // Destination of the match the completion came from, so the field icon can
  // show that site's favicon while the completion is showing.
  GURL inline_completion_url;
};

using OmniboxResultsCallback =
    base::RepeatingCallback<void(const OmniboxResults&)>;

// Data-only wrapper around Chromium's autocomplete stack.
//
// This exists so the overlay gets Chromium's matching, frecency ranking,
// bookmarks, shortcuts, search suggestions and inline completion without
// pulling in any of Chromium's omnibox UI.  Nothing in this class or its
// callers should reference omnibox view code: presentation belongs entirely
// to AvoraQuickNavView.
class OmniboxBridge : public AutocompleteController::Observer {
 public:
  OmniboxBridge(Profile* profile, OmniboxResultsCallback callback);
  OmniboxBridge(const OmniboxBridge&) = delete;
  OmniboxBridge& operator=(const OmniboxBridge&) = delete;
  ~OmniboxBridge() override;

  // Maximum rows handed back in a single result set.
  static constexpr size_t kMaxSuggestions = 8;

  // Starts a query for `text`.  `prevent_inline_autocomplete` must be true
  // whenever the user is deleting, so type-ahead does not immediately put back
  // the characters they just removed.
  void Start(const std::u16string& text,
             const GURL& current_url,
             bool prevent_inline_autocomplete);

  // Cancels the running query and drops its results.
  void Stop();

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

 private:
  raw_ptr<Profile> profile_;
  OmniboxResultsCallback callback_;
  std::unique_ptr<AutocompleteController> controller_;

  // Text most recently passed to Start(), echoed back on the results so the
  // view can discard stale passes.
  std::u16string current_query_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_OMNIBOX_BRIDGE_H_
