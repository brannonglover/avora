// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_SEARCH_SUGGEST_H_
#define CHROME_BROWSER_AVORA_AVORA_SEARCH_SUGGEST_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace avora {

// Fetches search suggestions from Google's Suggest API.  Each call to
// FetchSuggestions() cancels any in-flight request so that only the
// latest query produces a callback.
class SearchSuggestProvider {
 public:
  using SuggestionsCallback =
      base::OnceCallback<void(std::vector<std::string> suggestions)>;

  explicit SearchSuggestProvider(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~SearchSuggestProvider();

  SearchSuggestProvider(const SearchSuggestProvider&) = delete;
  SearchSuggestProvider& operator=(const SearchSuggestProvider&) = delete;

  // Fetches suggestions for |query|.  Any previous in-flight request is
  // cancelled.  |callback| is invoked on the UI thread with the results
  // (may be empty on network error or invalid response).
  void FetchSuggestions(const std::string& query,
                        SuggestionsCallback callback);

  // Cancels any in-flight request and drops the pending callback.
  void Cancel();

 private:
  void OnResponse(std::optional<std::string> response_body);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  SuggestionsCallback callback_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_SEARCH_SUGGEST_H_
