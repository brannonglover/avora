// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_omnibox_bridge.h"

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_enums.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/metrics_proto/omnibox_focus_type.pb.h"

namespace avora {

OmniboxBridge::OmniboxBridge(Profile* profile, OmniboxResultsCallback callback)
    : profile_(profile), callback_(std::move(callback)) {
  AutocompleteControllerConfig config;
  config.provider_types = AutocompleteClassifier::DefaultOmniboxProviders();
  // IPH matches are omnibox-UI affordances; the overlay has no place to draw
  // them and would otherwise spend a row on one.
  config.show_iph_matches = false;

  controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile), config);
  controller_->AddObserver(this);
}

OmniboxBridge::~OmniboxBridge() {
  controller_->RemoveObserver(this);
}

void OmniboxBridge::Start(const std::u16string& text,
                          const GURL& current_url,
                          bool prevent_inline_autocomplete) {
  current_query_ = text;

  if (text.empty()) {
    Stop();
    return;
  }

  AutocompleteInput input(text, metrics::OmniboxEventProto::OTHER,
                          ChromeAutocompleteSchemeClassifier(profile_));
  input.set_current_url(current_url);
  input.set_prevent_inline_autocomplete(prevent_inline_autocomplete);
  input.set_focus_type(metrics::OmniboxFocusType::INTERACTION_DEFAULT);
  // Async passes are what bring in remote search suggestions, which the
  // overlay wants; the sync pass alone would be history and bookmarks only.
  input.set_omit_asynchronous_matches(false);

  controller_->Start(input);
}

void OmniboxBridge::Stop() {
  current_query_.clear();
  controller_->Stop(AutocompleteStopReason::kClobbered);
}

void OmniboxBridge::OnResultChanged(AutocompleteController* controller,
                                    bool default_match_changed) {
  if (current_query_.empty()) {
    return;
  }

  OmniboxResults results;
  results.query = current_query_;

  const AutocompleteResult& result = controller->result();

  // Type-ahead comes only from the default match, which is the one Enter would
  // navigate to.  Completing to anything else would take the user somewhere
  // other than where the field says they are going.
  if (const AutocompleteMatch* default_match = result.default_match()) {
    if (!default_match->inline_autocompletion.empty()) {
      results.inline_completion = default_match->inline_autocompletion;
      results.inline_completion_url = default_match->destination_url;
    }
  }

  for (const AutocompleteMatch& match : result) {
    if (results.suggestions.size() >= kMaxSuggestions) {
      break;
    }

    OmniboxSuggestion suggestion;
    suggestion.is_search = AutocompleteMatch::IsSearchType(match.type);

    if (suggestion.is_search) {
      // `fill_into_edit` is the bare query for search matches; handing that
      // back keeps the user's configured Avora search engine in charge.
      suggestion.commit_value = base::UTF16ToUTF8(match.fill_into_edit);
      suggestion.title = match.contents;
    } else {
      if (!match.destination_url.is_valid()) {
        continue;
      }
      suggestion.commit_value = match.destination_url.spec();
      suggestion.destination_url = match.destination_url;
      suggestion.title =
          match.description.empty() ? match.contents : match.description;
      suggestion.subtitle = match.contents;
    }

    if (suggestion.commit_value.empty()) {
      continue;
    }

    results.suggestions.push_back(std::move(suggestion));
  }

  callback_.Run(results);
}

}  // namespace avora
