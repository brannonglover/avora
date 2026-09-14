// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_profile.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora {

namespace {

bool IsPrefRegistered(PrefService* ps) {
  return ps && ps->FindPreference(BrowserProfileStore::kProfilesPref);
}

// Storage partition names should stay conservative: lowercase alphanumeric
// only, so they're safe to use as on-disk directory names.
std::string SanitizePartitionName(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c)) {
      out.push_back(base::ToLowerASCII(c));
    }
  }
  return out;
}

}  // namespace

// ── BrowserProfile ──────────────────────────────────────────────────────────

base::DictValue BrowserProfile::ToDict() const {
  return base::DictValue()
      .Set("id", id)
      .Set("name", name)
      .Set("partition", partition);
}

BrowserProfile BrowserProfile::FromDict(const base::DictValue& dict) {
  BrowserProfile profile;
  if (const std::string* val = dict.FindString("id")) {
    profile.id = *val;
  }
  if (const std::string* val = dict.FindString("name")) {
    profile.name = *val;
  }
  if (const std::string* val = dict.FindString("partition")) {
    profile.partition = *val;
  }
  return profile;
}

// ── BrowserProfileStore ─────────────────────────────────────────────────────

BrowserProfileStore::BrowserProfileStore(PrefService* pref_service)
    : pref_service_(pref_service),
      pref_available_(IsPrefRegistered(pref_service)) {
  auto profiles = GetProfiles();
  if (profiles.empty()) {
    // Seed the default identity.  Its partition is intentionally empty so it
    // resolves to Chromium's default partition and existing cookies survive.
    BrowserProfile def;
    def.id = kDefaultProfileId;
    def.name = "Default";
    def.partition = std::string();
    profiles.push_back(std::move(def));
    Save(profiles);
  }

  if (pref_available_) {
    pref_registrar_.Init(pref_service);
    pref_registrar_.Add(
        kProfilesPref,
        base::BindRepeating(&BrowserProfileStore::OnProfilesPrefChanged,
                            base::Unretained(this)));
  }
}

void BrowserProfileStore::OnProfilesPrefChanged() {
  if (writing_) {
    return;
  }
  cache_dirty_ = true;
  NotifyChanged();
}

BrowserProfileStore::~BrowserProfileStore() = default;

// static
void BrowserProfileStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kProfilesPref);
}

void BrowserProfileStore::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void BrowserProfileStore::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void BrowserProfileStore::NotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnBrowserProfilesChanged();
  }
}

void BrowserProfileStore::RefreshCacheIfNeeded() const {
  if (!cache_dirty_) {
    return;
  }
  if (!pref_available_) {
    cache_dirty_ = false;
    return;
  }
  const base::ListValue& list = pref_service_->GetList(kProfilesPref);
  cached_.clear();
  for (const auto& val : list) {
    if (val.is_dict()) {
      cached_.push_back(BrowserProfile::FromDict(val.GetDict()));
    }
  }
  cache_dirty_ = false;
}

std::vector<BrowserProfile> BrowserProfileStore::GetProfiles() const {
  RefreshCacheIfNeeded();
  return cached_;
}

const BrowserProfile* BrowserProfileStore::GetProfileById(
    const std::string& id) const {
  RefreshCacheIfNeeded();
  for (const auto& profile : cached_) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

const BrowserProfile* BrowserProfileStore::GetDefaultProfile() const {
  if (const BrowserProfile* def = GetProfileById(kDefaultProfileId)) {
    return def;
  }
  RefreshCacheIfNeeded();
  return cached_.empty() ? nullptr : &cached_.front();
}

std::string BrowserProfileStore::CreateProfile(const std::string& name) {
  RefreshCacheIfNeeded();
  auto profiles = cached_;

  BrowserProfile profile;
  profile.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  profile.name = name;
  // Derive the partition from the id so it's stable and filesystem-safe.
  profile.partition = SanitizePartitionName(profile.id);

  const std::string new_id = profile.id;
  profiles.push_back(std::move(profile));
  Save(profiles);

  NotifyChanged();
  return new_id;
}

void BrowserProfileStore::RemoveProfile(const std::string& id) {
  if (id == kDefaultProfileId) {
    return;
  }
  RefreshCacheIfNeeded();
  auto profiles = cached_;
  if (profiles.size() <= 1) {
    return;
  }
  std::erase_if(profiles,
                [&id](const BrowserProfile& p) { return p.id == id; });
  Save(profiles);
  NotifyChanged();
}

void BrowserProfileStore::RenameProfile(const std::string& id,
                                        const std::string& new_name) {
  RefreshCacheIfNeeded();
  auto profiles = cached_;
  for (auto& profile : profiles) {
    if (profile.id == id) {
      profile.name = new_name;
      break;
    }
  }
  Save(profiles);
  NotifyChanged();
}

void BrowserProfileStore::Save(const std::vector<BrowserProfile>& profiles) {
  if (pref_available_) {
    base::ListValue list;
    for (const auto& profile : profiles) {
      list.Append(profile.ToDict());
    }
    writing_ = true;
    pref_service_->SetList(kProfilesPref, std::move(list));
    writing_ = false;
  }
  cached_ = profiles;
  cache_dirty_ = false;
}

}  // namespace avora
