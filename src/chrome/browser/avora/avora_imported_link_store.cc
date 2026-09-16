// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_imported_link_store.h"

#include <algorithm>
#include <string_view>

#include "base/functional/bind.h"
#include "base/uuid.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora {

namespace {

bool IsPrefRegistered(PrefService* ps, std::string_view pref_name) {
  return ps && ps->FindPreference(pref_name) != nullptr;
}

}  // namespace

// ── Design note: Space deletion ─────────────────────────────────────────────
//
// When an Avora Space is deleted, its imported sources are NOT automatically
// removed.  This is deliberate:
//
// 1. Destructive-by-default is dangerous — a user who deletes and recreates
//    a Space should not silently lose hundreds of imported bookmarks.
//
// 2. The caller that handles Space deletion (SpaceManager / UI) has full
//    context to decide.  It may:
//    (a) Call RemoveSourcesForSpace() to clean up.
//    (b) Reassign orphaned sources to another Space.
//    (c) Leave them as orphaned data that a future "manage imports" UI
//        can surface.
//
// Until a product decision is made, sources whose space_id no longer matches
// any existing Space are simply invisible (no Space's active_space_id will
// match them) but remain in storage.
// ─────────────────────────────────────────────────────────────────────────────

// ── Lifecycle ───────────────────────────────────────────────────────────────

ImportedLinkStore::ImportedLinkStore(PrefService* pref_service)
    : pref_service_(pref_service),
      pref_available_(IsPrefRegistered(pref_service, kImportedLinksPref)) {
  if (!pref_available_) {
    return;
  }
  pref_registrar_.Init(pref_service);
  pref_registrar_.Add(
      kImportedLinksPref,
      base::BindRepeating(&ImportedLinkStore::OnPrefChanged,
                          base::Unretained(this)));
}

ImportedLinkStore::~ImportedLinkStore() = default;

// static
void ImportedLinkStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kImportedLinksPref);
}

void ImportedLinkStore::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ImportedLinkStore::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ImportedLinkStore::NotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnImportedLinksChanged();
  }
}

void ImportedLinkStore::OnPrefChanged() {
  if (writing_) {
    return;
  }
  cache_dirty_ = true;
  NotifyChanged();
}

// ── Cache ───────────────────────────────────────────────────────────────────

void ImportedLinkStore::RefreshCacheIfNeeded() const {
  if (!cache_dirty_) {
    return;
  }
  if (!pref_available_) {
    cache_dirty_ = false;
    return;
  }
  const base::ListValue& list = pref_service_->GetList(kImportedLinksPref);
  cached_.clear();
  for (const auto& val : list) {
    if (val.is_dict()) {
      ImportedSource source = ImportedSource::FromDict(val.GetDict());
      if (!source.id.empty()) {
        cached_.push_back(std::move(source));
      }
    }
  }
  cache_dirty_ = false;
}

void ImportedLinkStore::Save(const std::vector<ImportedSource>& sources) {
  if (pref_available_) {
    base::ListValue list;
    for (const auto& source : sources) {
      list.Append(source.ToDict());
    }
    writing_ = true;
    pref_service_->SetList(kImportedLinksPref, std::move(list));
    writing_ = false;
  }
  cached_ = sources;
  cache_dirty_ = false;
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<ImportedSource> ImportedLinkStore::GetAllSources() const {
  RefreshCacheIfNeeded();
  return cached_;
}

std::vector<ImportedSource> ImportedLinkStore::GetSourcesForSpace(
    const std::string& space_id) const {
  RefreshCacheIfNeeded();
  std::vector<ImportedSource> result;
  for (const auto& source : cached_) {
    if (source.space_id == space_id) {
      result.push_back(source);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const ImportedSource& a, const ImportedSource& b) {
                     return a.imported_at < b.imported_at;
                   });
  return result;
}

const ImportedSource* ImportedLinkStore::GetSourceById(
    const std::string& source_id) const {
  RefreshCacheIfNeeded();
  for (const auto& source : cached_) {
    if (source.id == source_id) {
      return &source;
    }
  }
  return nullptr;
}

std::vector<ImportedItem> ImportedLinkStore::GetChildren(
    const std::string& source_id,
    const std::string& parent_id) const {
  RefreshCacheIfNeeded();
  for (const auto& source : cached_) {
    if (source.id != source_id) {
      continue;
    }
    std::vector<ImportedItem> result;
    for (const auto& item : source.items) {
      if (item.parent_id == parent_id) {
        result.push_back(item);
      }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const ImportedItem& a, const ImportedItem& b) {
                       return a.order < b.order;
                     });
    return result;
  }
  return {};
}

const ImportedItem* ImportedLinkStore::GetItemById(
    const std::string& source_id,
    const std::string& item_id) const {
  RefreshCacheIfNeeded();
  for (const auto& source : cached_) {
    if (source.id != source_id) {
      continue;
    }
    for (const auto& item : source.items) {
      if (item.id == item_id) {
        return &item;
      }
    }
    break;
  }
  return nullptr;
}

// ── Mutations ───────────────────────────────────────────────────────────────

std::string ImportedLinkStore::AddSource(ImportedSource source) {
  RefreshCacheIfNeeded();
  auto sources = cached_;

  if (source.id.empty()) {
    source.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }

  // Generate ids for any items that lack them.
  for (auto& item : source.items) {
    if (item.id.empty()) {
      item.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    }
  }

  const std::string result_id = source.id;

  // Replace existing source with the same id, or append.
  bool replaced = false;
  for (auto& existing : sources) {
    if (existing.id == source.id) {
      existing = std::move(source);
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    sources.push_back(std::move(source));
  }

  Save(sources);
  NotifyChanged();
  return result_id;
}

void ImportedLinkStore::RemoveSource(const std::string& source_id) {
  RefreshCacheIfNeeded();
  auto sources = cached_;
  const size_t before = sources.size();
  std::erase_if(sources, [&source_id](const ImportedSource& s) {
    return s.id == source_id;
  });
  if (sources.size() == before) {
    return;
  }
  Save(sources);
  NotifyChanged();
}

void ImportedLinkStore::RemoveItem(const std::string& source_id,
                                   const std::string& item_id) {
  RefreshCacheIfNeeded();
  auto sources = cached_;
  for (auto& source : sources) {
    if (source.id != source_id) {
      continue;
    }
    // Collect the item and all its descendants.
    std::vector<std::string> to_remove;
    to_remove.push_back(item_id);
    CollectDescendants(source.items, item_id, to_remove);

    const size_t before = source.items.size();
    std::erase_if(source.items, [&to_remove](const ImportedItem& item) {
      return std::find(to_remove.begin(), to_remove.end(), item.id) !=
             to_remove.end();
    });
    if (source.items.size() == before) {
      return;
    }
    Save(sources);
    NotifyChanged();
    return;
  }
}

void ImportedLinkStore::RemoveSourcesForSpace(const std::string& space_id) {
  if (space_id.empty()) {
    return;
  }
  RefreshCacheIfNeeded();
  auto sources = cached_;
  const size_t before = sources.size();
  std::erase_if(sources, [&space_id](const ImportedSource& s) {
    return s.space_id == space_id;
  });
  if (sources.size() == before) {
    return;
  }
  Save(sources);
  NotifyChanged();
}

// static
void ImportedLinkStore::CollectDescendants(
    const std::vector<ImportedItem>& items,
    const std::string& item_id,
    std::vector<std::string>& out) {
  for (const auto& item : items) {
    if (item.parent_id == item_id) {
      out.push_back(item.id);
      CollectDescendants(items, item.id, out);
    }
  }
}

}  // namespace avora
