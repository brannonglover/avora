// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_IMPORT_PROVENANCE_H_
#define CHROME_BROWSER_AVORA_AVORA_IMPORT_PROVENANCE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// Tracks where an imported Avora record came from, so re-running an importer
// updates the record it already created instead of duplicating it.
//
// Deliberately not specific to any one importer or record kind: any import
// source can key its own dedup on (record_type, record_id) -> external_id
// here, instead of adding an import-provenance field to every model struct
// it touches (Space, SidebarItem, PinnedFolder, ...), each with its own
// migration.  Mirrors how ImportedLinkStore already keeps bookmark-import
// data in its own pref rather than folded into another store.
struct ImportProvenance {
  // What kind of Avora record this describes, e.g. "space", "pinned_item",
  // "pinned_folder", "favorite_batch".  Caller-defined; opaque to this store.
  std::string record_type;

  // The Avora id of the record this provenance describes (e.g. a Space's id,
  // a SidebarItem's id).  Combined with |record_type| this is the primary
  // lookup key, and is unique: Set() replaces any existing provenance for
  // the same (record_type, record_id).
  std::string record_id;

  // Where this record came from, e.g. "arc".
  std::string import_source;

  // Stable identifier from the source system (e.g. Arc's own UUID for the
  // Space, item, folder, or profile this was derived from).  Never the
  // record's own Avora id, and never a URL or title -- those can change
  // without the underlying source object's identity changing, and matching
  // on them would silently duplicate on re-import after a rename.
  std::string external_id;

  // Groups every record created by one run of an importer.  Not currently
  // consumed by anything, but present from the start so a future "undo this
  // import" doesn't need a schema change to retrofit it.
  std::string import_batch_id;

  base::DictValue ToDict() const;
  static ImportProvenance FromDict(const base::DictValue& dict);
};

// Persists import provenance for arbitrary Avora records in a single pref.
//
// Like SidebarItemStore, instances are cheap and self-syncing: each one
// watches the backing pref, so several components can own their own store
// and still see each other's writes.
class ImportProvenanceStore {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnProvenanceChanged() {}
  };

  static constexpr char kProvenancePref[] = "avora.import_provenance";

  explicit ImportProvenanceStore(PrefService* pref_service);
  ~ImportProvenanceStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Every provenance record, unordered.
  std::vector<ImportProvenance> GetAll() const;

  // Looks up provenance by the Avora record it describes.
  const ImportProvenance* GetByRecord(const std::string& record_type,
                                      const std::string& record_id) const;

  // Looks up provenance by the source system's own stable id, scoped to one
  // import source and record kind.  This is the dedup lookup: "have we
  // already imported this external object as this kind of Avora record?"
  const ImportProvenance* GetByExternalId(
      const std::string& import_source,
      const std::string& record_type,
      const std::string& external_id) const;

  // Replaces any existing provenance for the same (record_type, record_id).
  void Set(const ImportProvenance& provenance);

  void RemoveByRecord(const std::string& record_type,
                      const std::string& record_id);

 private:
  void Save(const std::vector<ImportProvenance>& items);
  void RefreshCacheIfNeeded() const;
  void NotifyChanged();

  // Fired when another ImportProvenanceStore instance writes the backing
  // pref.
  void OnProvenancePrefChanged();

  raw_ptr<PrefService> pref_service_;
  bool pref_available_ = false;

  // Set while this instance is writing, to suppress duplicate notification.
  bool writing_ = false;

  mutable std::vector<ImportProvenance> cached_;
  mutable bool cache_dirty_ = true;

  PrefChangeRegistrar pref_registrar_;

  base::ObserverList<Observer> observers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_IMPORT_PROVENANCE_H_
