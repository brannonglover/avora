// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_pinned_items.h"

#include <algorithm>

#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/url_formatter/url_fixer.h"
#include "components/url_formatter/url_formatter.h"
#include "net/base/url_util.h"
#include "url/third_party/mozilla/url_parse.h"
#include "url/url_constants.h"

namespace avora {

// static
void PinnedItemsManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kNativePinnedTabsBackfilledPref, false);
}

PinnedItemsManager::PinnedItemsManager(PrefService* prefs) : prefs_(prefs) {
  if (!prefs_) {
    return;
  }

  space_manager_ = std::make_unique<SpaceManager>(prefs_);
  item_store_ = std::make_unique<SidebarItemStore>(prefs_);

  item_store_->AddObserver(this);
  space_manager_->AddObserver(this);
}

PinnedItemsManager::~PinnedItemsManager() {
  if (item_store_) {
    item_store_->RemoveObserver(this);
  }
  if (space_manager_) {
    space_manager_->RemoveObserver(this);
  }
}

// ── Space scoping ───────────────────────────────────────────────────────────

std::string PinnedItemsManager::ActiveSpaceId() const {
  if (!window_active_space_id_.empty()) {
    return window_active_space_id_;
  }
  if (space_manager_) {
    if (const Space* active = space_manager_->GetActiveSpace()) {
      return active->id;
    }
  }
  return std::string();
}

void PinnedItemsManager::SetWindowActiveSpaceId(const std::string& id) {
  if (id == window_active_space_id_) {
    return;
  }
  window_active_space_id_ = id;
  NotifyChanged();
}

std::vector<SidebarItem> PinnedItemsManager::ActivePinnedItems() const {
  if (!item_store_) {
    return {};
  }
  return item_store_->GetItems(ActiveSpaceId(), SidebarItemType::kPinned);
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<PinnedItemEntry> PinnedItemsManager::GetPinnedItems() const {
  std::vector<PinnedItemEntry> result;
  for (const auto& item : ActivePinnedItems()) {
    result.push_back({item.url, item.title, item.id});
  }
  return result;
}

std::string PinnedItemsManager::GetActiveSpaceId() const {
  return ActiveSpaceId();
}

std::string PinnedItemsManager::GetPinnedItemIdForUrl(
    const std::string& url) const {
  for (const auto& item : ActivePinnedItems()) {
    if (item.url == url) {
      return item.id;
    }
  }
  return std::string();
}

std::string PinnedItemsManager::GetPinnedItemUrlById(
    const std::string& id) const {
  if (!item_store_) {
    return std::string();
  }
  if (const SidebarItem* item = item_store_->GetItemById(id)) {
    return item->url;
  }
  return std::string();
}

std::optional<PinnedItemEntry> PinnedItemsManager::GetPinnedItemById(
    const std::string& id) const {
  if (!item_store_) {
    return std::nullopt;
  }
  const SidebarItem* item = item_store_->GetItemById(id);
  if (!item || item->type != SidebarItemType::kPinned) {
    return std::nullopt;
  }
  return PinnedItemEntry{item->url, item->title, item->id};
}

bool PinnedItemsManager::IsPinned(const std::string& url) const {
  for (const auto& item : ActivePinnedItems()) {
    if (item.url == url) {
      return true;
    }
  }
  return false;
}

// ── Mutations ───────────────────────────────────────────────────────────────

std::string PinnedItemsManager::AddPinnedItem(const std::string& url,
                                              const std::string& title) {
  if (!item_store_ || IsPinned(url)) {
    return std::string();
  }
  return item_store_->AddItem(ActiveSpaceId(), SidebarItemType::kPinned, url,
                              title);
}

std::string PinnedItemsManager::InsertPinnedItemAt(int index,
                                                   const std::string& url,
                                                   const std::string& title) {
  if (!item_store_ || IsPinned(url)) {
    return std::string();
  }

  const std::string new_id = item_store_->AddItem(
      ActiveSpaceId(), SidebarItemType::kPinned, url, title);

  // AddItem appends; shuffle the new item into the requested slot.
  std::vector<std::string> ids;
  for (const auto& item : ActivePinnedItems()) {
    if (item.id != new_id) {
      ids.push_back(item.id);
    }
  }
  const int clamped = std::clamp(index, 0, static_cast<int>(ids.size()));
  ids.insert(ids.begin() + clamped, new_id);
  item_store_->ReorderItems(ids);
  return new_id;
}

void PinnedItemsManager::RemovePinnedItem(const std::string& id) {
  if (!item_store_) {
    return;
  }
  item_store_->RemoveItem(id);
}

// static
GURL PinnedItemsManager::FixupPinnedItemUrl(std::string_view input) {
  const std::string trimmed(
      base::TrimWhitespaceASCII(input, base::TRIM_ALL));
  if (trimmed.empty()) {
    return GURL();
  }

  GURL url = url_formatter::FixupURL(trimmed, /*desired_tld=*/std::string());
  if (!url.is_valid() || url.SchemeIs(url::kJavaScriptScheme)) {
    return GURL();
  }

  // FixupURL() assumes http for a scheme-less host, as it always has; the
  // omnibox then upgrades a typed navigation like that to https.  Do the same
  // here so the pin stores where it will actually land -- except for hosts
  // that are usually plain http on purpose: a dev server or a router.
  url::Parsed parts;
  url_formatter::SegmentURL(trimmed, &parts);
  const bool typed_scheme = parts.scheme.is_nonempty();
  if (!typed_scheme && url.SchemeIs(url::kHttpScheme) &&
      !net::IsLocalhost(url) && !url.HostIsIPAddress()) {
    GURL::Replacements https;
    https.SetSchemeStr(url::kHttpsScheme);
    url = url.ReplaceComponents(https);
  }
  return url;
}

PinnedItemsManager::UrlEditCheck PinnedItemsManager::CheckPinnedItemUrl(
    const std::string& id,
    std::string_view input) const {
  if (!item_store_) {
    return UrlEditCheck::kNoSuchItem;
  }
  const SidebarItem* item = item_store_->GetItemById(id);
  if (!item || item->type != SidebarItemType::kPinned) {
    return UrlEditCheck::kNoSuchItem;
  }
  const GURL url = FixupPinnedItemUrl(input);
  if (!url.is_valid()) {
    return UrlEditCheck::kInvalidUrl;
  }
  if (url.spec() == item->url) {
    return UrlEditCheck::kUnchanged;
  }
  // The item's own Space, not the active one: a duplicate only matters among
  // the pins it sits beside, wherever this manager happens to be looking.
  for (const auto& other :
       item_store_->GetItems(item->space_id, SidebarItemType::kPinned)) {
    if (other.id != id && other.url == url.spec()) {
      return UrlEditCheck::kAlreadyPinned;
    }
  }
  return UrlEditCheck::kOk;
}

bool PinnedItemsManager::SetPinnedItemUrl(const std::string& id,
                                          std::string_view input) {
  switch (CheckPinnedItemUrl(id, input)) {
    case UrlEditCheck::kOk:
      break;
    case UrlEditCheck::kUnchanged:
      return true;
    case UrlEditCheck::kInvalidUrl:
    case UrlEditCheck::kAlreadyPinned:
    case UrlEditCheck::kNoSuchItem:
      return false;
  }

  const GURL url = FixupPinnedItemUrl(input);
  const std::string title = base::UTF16ToUTF8(url_formatter::FormatUrl(
      url,
      url_formatter::kFormatUrlOmitDefaults |
          url_formatter::kFormatUrlOmitHTTPS |
          url_formatter::kFormatUrlOmitTrivialSubdomains,
      base::UnescapeRule::SPACES, nullptr, nullptr, nullptr));
  item_store_->SetItemUrlAndTitle(id, url.spec(), title);
  return true;
}

void PinnedItemsManager::MovePinnedItem(int from_index, int to_index) {
  if (!item_store_) {
    return;
  }

  std::vector<std::string> ids;
  for (const auto& item : ActivePinnedItems()) {
    ids.push_back(item.id);
  }

  const int count = static_cast<int>(ids.size());
  if (from_index < 0 || from_index >= count || to_index < 0 ||
      to_index >= count || from_index == to_index) {
    return;
  }

  const std::string moved = ids[from_index];
  ids.erase(ids.begin() + from_index);
  ids.insert(ids.begin() + to_index, moved);
  item_store_->ReorderItems(ids);
}

void PinnedItemsManager::ReorderAllItems(
    const std::vector<std::string>& ordered_ids) {
  if (!item_store_) {
    return;
  }
  item_store_->ReorderItems(ordered_ids);
}

void PinnedItemsManager::MoveItemToSpace(
    const std::string& id,
    const std::string& destination_space_id) {
  if (!item_store_) {
    return;
  }
  item_store_->MoveItemToSpace(id, destination_space_id);
}

// ── Observers ───────────────────────────────────────────────────────────────

void PinnedItemsManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PinnedItemsManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void PinnedItemsManager::OnSidebarItemsChanged() {
  NotifyChanged();
}

void PinnedItemsManager::OnActiveSpaceChanged(const std::string& space_id) {
  // When window-local active Space is set, the global pref change is
  // irrelevant for this instance; the owning view drives changes via
  // SetWindowActiveSpaceId() instead.
  if (!window_active_space_id_.empty()) {
    return;
  }
  // A different Space is showing, so the pinned-items set has changed
  // wholesale.
  NotifyChanged();
}

void PinnedItemsManager::NotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnPinnedItemsChanged();
  }
}

}  // namespace avora
