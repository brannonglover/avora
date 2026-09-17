// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_import_provenance.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora {

namespace {

bool IsPrefRegistered(PrefService* ps) {
  return ps && ps->FindPreference(ImportProvenanceStore::kProvenancePref);
}

}  // namespace

// ── ImportProvenance ────────────────────────────────────────────────────────

base::DictValue ImportProvenance::ToDict() const {
  return base::DictValue()
      .Set("record_type", record_type)
      .Set("record_id", record_id)
      .Set("import_source", import_source)
      .Set("external_id", external_id)
      .Set("import_batch_id", import_batch_id);
}

ImportProvenance ImportProvenance::FromDict(const base::DictValue& dict) {
  ImportProvenance provenance;
  if (const std::string* val = dict.FindString("record_type")) {
    provenance.record_type = *val;
  }
  if (const std::string* val = dict.FindString("record_id")) {
    provenance.record_id = *val;
  }
  if (const std::string* val = dict.FindString("import_source")) {
    provenance.import_source = *val;
  }
  if (const std::string* val = dict.FindString("external_id")) {
    provenance.external_id = *val;
  }
  if (const std::string* val = dict.FindString("import_batch_id")) {
    provenance.import_batch_id = *val;
  }
  return provenance;
}

// ── ImportProvenanceStore ───────────────────────────────────────────────────

ImportProvenanceStore::ImportProvenanceStore(PrefService* pref_service)
    : pref_service_(pref_service),
      pref_available_(IsPrefRegistered(pref_service)) {
  if (!pref_available_) {
    return;
  }
  pref_registrar_.Init(pref_service);
  pref_registrar_.Add(
      kProvenancePref,
      base::BindRepeating(&ImportProvenanceStore::OnProvenancePrefChanged,
                          base::Unretained(this)));
}

ImportProvenanceStore::~ImportProvenanceStore() = default;

void ImportProvenanceStore::OnProvenancePrefChanged() {
  if (writing_) {
    return;
  }
  cache_dirty_ = true;
  NotifyChanged();
}

// static
void ImportProvenanceStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kProvenancePref);
}

void ImportProvenanceStore::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ImportProvenanceStore::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ImportProvenanceStore::NotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnProvenanceChanged();
  }
}

void ImportProvenanceStore::RefreshCacheIfNeeded() const {
  if (!cache_dirty_) {
    return;
  }
  if (!pref_available_) {
    cache_dirty_ = false;
    return;
  }
  const base::ListValue& list = pref_service_->GetList(kProvenancePref);
  cached_.clear();
  for (const auto& val : list) {
    if (val.is_dict()) {
      cached_.push_back(ImportProvenance::FromDict(val.GetDict()));
    }
  }
  cache_dirty_ = false;
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::vector<ImportProvenance> ImportProvenanceStore::GetAll() const {
  RefreshCacheIfNeeded();
  return cached_;
}

const ImportProvenance* ImportProvenanceStore::GetByRecord(
    const std::string& record_type,
    const std::string& record_id) const {
  RefreshCacheIfNeeded();
  for (const auto& provenance : cached_) {
    if (provenance.record_type == record_type &&
        provenance.record_id == record_id) {
      return &provenance;
    }
  }
  return nullptr;
}

const ImportProvenance* ImportProvenanceStore::GetByExternalId(
    const std::string& import_source,
    const std::string& record_type,
    const std::string& external_id) const {
  RefreshCacheIfNeeded();
  for (const auto& provenance : cached_) {
    if (provenance.import_source == import_source &&
        provenance.record_type == record_type &&
        provenance.external_id == external_id) {
      return &provenance;
    }
  }
  return nullptr;
}

// ── Mutations ───────────────────────────────────────────────────────────────

void ImportProvenanceStore::Set(const ImportProvenance& provenance) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  std::erase_if(items, [&provenance](const ImportProvenance& existing) {
    return existing.record_type == provenance.record_type &&
          existing.record_id == provenance.record_id;
  });
  items.push_back(provenance);
  Save(items);
  NotifyChanged();
}

void ImportProvenanceStore::RemoveByRecord(const std::string& record_type,
                                           const std::string& record_id) {
  RefreshCacheIfNeeded();
  auto items = cached_;
  const size_t before = items.size();
  std::erase_if(items, [&](const ImportProvenance& existing) {
    return existing.record_type == record_type &&
          existing.record_id == record_id;
  });
  if (items.size() == before) {
    return;
  }
  Save(items);
  NotifyChanged();
}

void ImportProvenanceStore::Save(const std::vector<ImportProvenance>& items) {
  if (pref_available_) {
    base::ListValue list;
    for (const auto& item : items) {
      list.Append(item.ToDict());
    }
    writing_ = true;
    pref_service_->SetList(kProvenancePref, std::move(list));
    writing_ = false;
  }
  cached_ = items;
  cache_dirty_ = false;
}

}  // namespace avora
