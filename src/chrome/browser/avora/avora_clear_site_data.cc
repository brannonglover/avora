// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_clear_site_data.h"

#include <array>
#include <memory>
#include <utility>

#include "base/functional/callback.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace avora {

namespace {

// Display order: the order the rows appear in the overlay.
constexpr std::array<SiteDataCommand, 2> kAllCommands = {
    SiteDataCommand::kClearCache,
    SiteDataCommand::kClearCookies,
};

uint64_t RemoveMaskFor(SiteDataCommand command) {
  switch (command) {
    case SiteDataCommand::kClearCache:
      return content::BrowsingDataRemover::DATA_TYPE_CACHE;
    case SiteDataCommand::kClearCookies:
      return content::BrowsingDataRemover::DATA_TYPE_COOKIES;
  }
}

// Owns itself from Start() until the remover reports back.  The remover
// reports even for tasks still queued when it shuts down, so this never leaks
// and never outlives its BrowserContext.  It only reports to observers that
// are registered with it, which the scoped observation takes care of.
class SiteDataRemoval : public content::BrowsingDataRemover::Observer {
 public:
  static void Start(content::BrowserContext* browser_context,
                    uint64_t remove_mask,
                    std::unique_ptr<content::BrowsingDataFilterBuilder> filter,
                    base::OnceCallback<void(bool)> done) {
    content::BrowsingDataRemover* remover =
        browser_context->GetBrowsingDataRemover();
    auto* removal = new SiteDataRemoval(remover, std::move(done));
    remover->RemoveWithFilterAndReply(
        base::Time(), base::Time::Max(), remove_mask,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
            content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB,
        std::move(filter), removal);
  }

  // content::BrowsingDataRemover::Observer:
  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    base::OnceCallback<void(bool)> done = std::move(done_);
    delete this;
    std::move(done).Run(failed_data_types == 0);
  }

 private:
  SiteDataRemoval(content::BrowsingDataRemover* remover,
                  base::OnceCallback<void(bool)> done)
      : done_(std::move(done)) {
    observation_.Observe(remover);
  }
  ~SiteDataRemoval() override = default;

  base::OnceCallback<void(bool)> done_;
  base::ScopedObservation<content::BrowsingDataRemover,
                          content::BrowsingDataRemover::Observer>
      observation_{this};
};

}  // namespace

std::u16string SiteDataCommandName(SiteDataCommand command) {
  switch (command) {
    case SiteDataCommand::kClearCache:
      return u"Clear Cache";
    case SiteDataCommand::kClearCookies:
      return u"Clear Cookies";
  }
}

std::optional<SiteDataCommand> MatchSiteDataCommand(std::u16string_view text) {
  const std::u16string_view trimmed =
      base::TrimWhitespace(text, base::TRIM_ALL);
  for (SiteDataCommand command : kAllCommands) {
    if (base::EqualsCaseInsensitiveASCII(trimmed,
                                         SiteDataCommandName(command))) {
      return command;
    }
  }
  return std::nullopt;
}

std::vector<SiteDataCommand> SiteDataCommandsStartingWith(
    std::u16string_view text) {
  const std::u16string_view trimmed =
      base::TrimWhitespace(text, base::TRIM_LEADING);
  std::vector<SiteDataCommand> matches;
  if (trimmed.size() < kMinCommandPrefixLength) {
    return matches;
  }
  for (SiteDataCommand command : kAllCommands) {
    if (base::StartsWith(SiteDataCommandName(command), trimmed,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      matches.push_back(command);
    }
  }
  return matches;
}

std::string ClearableSiteForURL(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.host().empty()) {
    return std::string();
  }
  std::string site = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return site.empty() ? std::string(url.host()) : site;
}

void ClearSiteData(content::WebContents* contents,
                   SiteDataCommand command,
                   base::OnceCallback<void(bool success)> done) {
  const std::string site =
      contents ? ClearableSiteForURL(contents->GetLastCommittedURL())
               : std::string();
  if (site.empty()) {
    std::move(done).Run(false);
    return;
  }

  auto filter = content::BrowsingDataFilterBuilder::Create(
      content::BrowsingDataFilterBuilder::Mode::kDelete);
  filter->AddRegisterableDomain(site);

  // Unset, the remover only touches the default partition, which is the jar
  // of the default identity alone.  Every other Space keeps its own.
  const content::StoragePartitionConfig& partition =
      contents->GetPrimaryMainFrame()->GetStoragePartition()->GetConfig();
  if (!partition.is_default()) {
    filter->SetStoragePartitionConfig(partition);
  }

  SiteDataRemoval::Start(contents->GetBrowserContext(), RemoveMaskFor(command),
                         std::move(filter), std::move(done));
}

}  // namespace avora
