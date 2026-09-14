// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_search_suggest.h"

#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/escape.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace avora {

namespace {

constexpr char kSuggestUrlPrefix[] =
    "https://suggestqueries.google.com/complete/search?client=chrome&q=";

constexpr int kMaxResponseBytes = 256 * 1024;  // 256 KB

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("avora_search_suggest", R"(
        semantics {
          sender: "Avora Search Suggestions"
          description:
            "Provides search query suggestions as the user types in the "
            "quick-navigation overlay."
          trigger:
            "The user types text in the quick-navigation search field."
          data: "The text the user has typed so far."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature is always active when the quick-navigation "
            "overlay is open."
        })");

}  // namespace

SearchSuggestProvider::SearchSuggestProvider(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}

SearchSuggestProvider::~SearchSuggestProvider() = default;

void SearchSuggestProvider::FetchSuggestions(const std::string& query,
                                             SuggestionsCallback callback) {
  Cancel();
  callback_ = std::move(callback);

  std::string escaped = base::EscapeQueryParamValue(query, /*use_plus=*/true);
  GURL url(kSuggestUrlPrefix + escaped);
  if (!url.is_valid()) {
    std::move(callback_).Run({});
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  url_loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  url_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&SearchSuggestProvider::OnResponse,
                     base::Unretained(this)),
      kMaxResponseBytes);
}

void SearchSuggestProvider::Cancel() {
  url_loader_.reset();
  callback_.Reset();
}

void SearchSuggestProvider::OnResponse(
    std::optional<std::string> response_body) {
  std::vector<std::string> suggestions;

  if (response_body.has_value()) {
    auto parsed = base::JSONReader::Read(
        *response_body, base::JSON_PARSE_RFC);
    // Google Suggest response: ["query",["sugg1","sugg2",...], ...]
    if (parsed && parsed->is_list()) {
      const auto& list = parsed->GetList();
      if (list.size() >= 2 && list[1].is_list()) {
        for (const auto& item : list[1].GetList()) {
          if (item.is_string()) {
            suggestions.push_back(item.GetString());
          }
        }
      }
    }
  }

  if (callback_) {
    std::move(callback_).Run(std::move(suggestions));
  }
}

}  // namespace avora
