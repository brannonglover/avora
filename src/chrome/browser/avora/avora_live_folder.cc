// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_live_folder.h"

#include <algorithm>

#include "base/json/values_util.h"

namespace avora {

namespace {

constexpr char kProviderGitHub[] = "github";
constexpr char kProviderUnknown[] = "unknown";

constexpr char kStatusOpen[] = "open";
constexpr char kStatusDraft[] = "draft";
constexpr char kStatusMerged[] = "merged";
constexpr char kStatusClosed[] = "closed";

}  // namespace

// ── Enums ───────────────────────────────────────────────────────────────────

std::string LiveFolderProviderIdToString(LiveFolderProviderId id) {
  switch (id) {
    case LiveFolderProviderId::kGitHub:
      return kProviderGitHub;
    case LiveFolderProviderId::kUnknown:
      return kProviderUnknown;
  }
  return kProviderUnknown;
}

LiveFolderProviderId LiveFolderProviderIdFromString(const std::string& value) {
  if (value == kProviderGitHub) {
    return LiveFolderProviderId::kGitHub;
  }
  return LiveFolderProviderId::kUnknown;
}

std::string LiveFolderItemStatusToString(LiveFolderItemStatus status) {
  switch (status) {
    case LiveFolderItemStatus::kOpen:
      return kStatusOpen;
    case LiveFolderItemStatus::kDraft:
      return kStatusDraft;
    case LiveFolderItemStatus::kMerged:
      return kStatusMerged;
    case LiveFolderItemStatus::kClosed:
      return kStatusClosed;
  }
  return kStatusOpen;
}

LiveFolderItemStatus LiveFolderItemStatusFromString(const std::string& value) {
  if (value == kStatusDraft) {
    return LiveFolderItemStatus::kDraft;
  }
  if (value == kStatusMerged) {
    return LiveFolderItemStatus::kMerged;
  }
  if (value == kStatusClosed) {
    return LiveFolderItemStatus::kClosed;
  }
  return LiveFolderItemStatus::kOpen;
}

bool IsActiveLiveFolderItemStatus(LiveFolderItemStatus status) {
  return status == LiveFolderItemStatus::kOpen ||
         status == LiveFolderItemStatus::kDraft;
}

// ── LiveFolderItemMetadata ──────────────────────────────────────────────────

base::DictValue LiveFolderItemMetadata::ToDict() const {
  return base::DictValue()
      .Set("repo", repo)
      .Set("author", author)
      .Set("number", number);
}

LiveFolderItemMetadata LiveFolderItemMetadata::FromDict(
    const base::DictValue& dict) {
  LiveFolderItemMetadata metadata;
  if (const std::string* val = dict.FindString("repo")) {
    metadata.repo = *val;
  }
  if (const std::string* val = dict.FindString("author")) {
    metadata.author = *val;
  }
  if (std::optional<int> val = dict.FindInt("number")) {
    metadata.number = *val;
  }
  return metadata;
}

// ── LiveFolderItem ──────────────────────────────────────────────────────────

base::DictValue LiveFolderItem::ToDict() const {
  return base::DictValue()
      .Set("external_id", external_id)
      .Set("title", title)
      .Set("url", url)
      .Set("status", LiveFolderItemStatusToString(status))
      .Set("metadata", metadata.ToDict())
      .Set("updated_at", base::TimeToValue(updated_at))
      .Set("seen", seen);
}

LiveFolderItem LiveFolderItem::FromDict(const base::DictValue& dict) {
  LiveFolderItem item;
  if (const std::string* val = dict.FindString("external_id")) {
    item.external_id = *val;
  }
  if (const std::string* val = dict.FindString("title")) {
    item.title = *val;
  }
  if (const std::string* val = dict.FindString("url")) {
    item.url = *val;
  }
  if (const std::string* val = dict.FindString("status")) {
    item.status = LiveFolderItemStatusFromString(*val);
  }
  if (const base::DictValue* val = dict.FindDict("metadata")) {
    item.metadata = LiveFolderItemMetadata::FromDict(*val);
  }
  if (const base::Value* val = dict.Find("updated_at")) {
    if (std::optional<base::Time> time = base::ValueToTime(*val)) {
      item.updated_at = *time;
    }
  }
  item.seen = dict.FindBool("seen").value_or(false);
  return item;
}

// ── LiveFolderQuery ─────────────────────────────────────────────────────────

bool LiveFolderQuery::HasNoInvolvementFilter() const {
  return !created_by_me && !assigned_to_me && !review_requested &&
         !mentioned && !team_review_requested;
}

// static
LiveFolderQuery LiveFolderQuery::DefaultForGitHub() {
  LiveFolderQuery query;
  query.created_by_me = true;
  query.assigned_to_me = true;
  query.review_requested = true;
  query.mentioned = true;
  query.team_review_requested = true;
  query.include_drafts = true;
  return query;
}

base::DictValue LiveFolderQuery::ToDict() const {
  base::ListValue repos;
  for (const auto& repo : repositories) {
    repos.Append(repo);
  }
  return base::DictValue()
      .Set("repositories", std::move(repos))
      .Set("created_by_me", created_by_me)
      .Set("assigned_to_me", assigned_to_me)
      .Set("review_requested", review_requested)
      .Set("mentioned", mentioned)
      .Set("team_review_requested", team_review_requested)
      .Set("include_drafts", include_drafts);
}

LiveFolderQuery LiveFolderQuery::FromDict(const base::DictValue& dict) {
  LiveFolderQuery query;
  if (const base::ListValue* repos = dict.FindList("repositories")) {
    for (const auto& value : *repos) {
      if (value.is_string()) {
        query.repositories.push_back(value.GetString());
      }
    }
  }
  query.created_by_me = dict.FindBool("created_by_me").value_or(false);
  query.assigned_to_me = dict.FindBool("assigned_to_me").value_or(false);
  query.review_requested = dict.FindBool("review_requested").value_or(false);
  query.mentioned = dict.FindBool("mentioned").value_or(false);
  query.team_review_requested =
      dict.FindBool("team_review_requested").value_or(false);
  query.include_drafts = dict.FindBool("include_drafts").value_or(true);
  return query;
}

// ── LiveFolder ──────────────────────────────────────────────────────────────

int LiveFolder::UnseenCount() const {
  int count = 0;
  for (const auto& item : items) {
    if (!item.seen && IsActiveLiveFolderItemStatus(item.status)) {
      ++count;
    }
  }
  return count;
}

std::vector<LiveFolderItem> LiveFolder::ActiveItems() const {
  std::vector<LiveFolderItem> active;
  for (const auto& item : items) {
    if (IsActiveLiveFolderItemStatus(item.status)) {
      active.push_back(item);
    }
  }
  std::stable_sort(active.begin(), active.end(),
                   [](const LiveFolderItem& a, const LiveFolderItem& b) {
                     return a.updated_at > b.updated_at;
                   });
  return active;
}

base::DictValue LiveFolder::ToDict() const {
  base::ListValue item_list;
  for (const auto& item : items) {
    item_list.Append(item.ToDict());
  }
  return base::DictValue()
      .Set("id", id)
      .Set("space_id", space_id)
      .Set("name", name)
      .Set("provider", LiveFolderProviderIdToString(provider))
      .Set("query", query.ToDict())
      .Set("items", std::move(item_list))
      .Set("expanded", expanded)
      .Set("order", order)
      .Set("last_synced_at", base::TimeToValue(last_synced_at));
}

LiveFolder LiveFolder::FromDict(const base::DictValue& dict) {
  LiveFolder folder;
  if (const std::string* val = dict.FindString("id")) {
    folder.id = *val;
  }
  if (const std::string* val = dict.FindString("space_id")) {
    folder.space_id = *val;
  }
  if (const std::string* val = dict.FindString("name")) {
    folder.name = *val;
  }
  if (const std::string* val = dict.FindString("provider")) {
    folder.provider = LiveFolderProviderIdFromString(*val);
  }
  if (const base::DictValue* val = dict.FindDict("query")) {
    folder.query = LiveFolderQuery::FromDict(*val);
  }
  if (const base::ListValue* val = dict.FindList("items")) {
    for (const auto& entry : *val) {
      if (entry.is_dict()) {
        folder.items.push_back(LiveFolderItem::FromDict(entry.GetDict()));
      }
    }
  }
  folder.expanded = dict.FindBool("expanded").value_or(true);
  if (std::optional<int> val = dict.FindInt("order")) {
    folder.order = *val;
  }
  if (const base::Value* val = dict.Find("last_synced_at")) {
    if (std::optional<base::Time> time = base::ValueToTime(*val)) {
      folder.last_synced_at = *time;
    }
  }
  return folder;
}

}  // namespace avora
