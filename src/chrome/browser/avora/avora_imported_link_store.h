// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_IMPORTED_LINK_STORE_H_
#define CHROME_BROWSER_AVORA_AVORA_IMPORTED_LINK_STORE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chrome/browser/avora/avora_imported_links.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;
class PrefRegistrySimple;

namespace avora {

// Persists imported browser bookmarks at the profile level.
//
// Each imported source belongs to a specific Avora Space (via
// ImportedSource::space_id).  The UI layer filters by Space using the
// window's WindowSpaceState::active_space_id() — never the global
// SpaceManager::GetActiveSpace().
//
// Like SidebarItemStore, instances are cheap and self-syncing: each one
// watches the backing pref, so several components can own their own store
// and still see each other's writes.
//
// Views and controllers interact exclusively through this API.  The
// underlying storage backend (currently prefs) is an implementation detail
// that may change to SQLite without affecting consumers.
class ImportedLinkStore {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnImportedLinksChanged() {}
  };

  explicit ImportedLinkStore(PrefService* pref_service);
  ImportedLinkStore(const ImportedLinkStore&) = delete;
  ImportedLinkStore& operator=(const ImportedLinkStore&) = delete;
  ~ImportedLinkStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // ── Queries ─────────────────────────────────────────────────────────────

  // All sources, regardless of Space.
  std::vector<ImportedSource> GetAllSources() const;

  // Sources belonging to |space_id|, ordered by imported_at (oldest first).
  std::vector<ImportedSource> GetSourcesForSpace(
      const std::string& space_id) const;

  // A single source by id, or nullptr if not found.  The pointer is
  // invalidated by any mutation.
  const ImportedSource* GetSourceById(const std::string& source_id) const;

  // Direct children of |parent_id| within |source_id|, sorted by order.
  // Pass an empty |parent_id| for root-level items.
  std::vector<ImportedItem> GetChildren(const std::string& source_id,
                                        const std::string& parent_id) const;

  // A single item within a source, or nullptr if not found.  The pointer
  // is invalidated by any mutation.
  const ImportedItem* GetItemById(const std::string& source_id,
                                  const std::string& item_id) const;

  // ── Mutations ───────────────────────────────────────────────────────────

  // Stores a fully populated source (including all items).  If a source
  // with the same id already exists it is replaced.  Generates an id if
  // |source.id| is empty.  Returns the source id.
  std::string AddSource(ImportedSource source);

  // Removes an entire import and all its items.  No-op if not found.
  void RemoveSource(const std::string& source_id);

  // Removes a single item.  If the item is a folder, all descendants are
  // removed as well.  No-op if the source or item is not found.
  void RemoveItem(const std::string& source_id, const std::string& item_id);

  // Removes every source belonging to |space_id|.
  //
  // NOTE: This is intentionally NOT called automatically when a Space is
  // deleted.  Whether imported data should be treated as orphaned or
  // destroyed is a product decision.  See the design note in the .cc file.
  void RemoveSourcesForSpace(const std::string& space_id);

 private:
  static constexpr char kImportedLinksPref[] = "avora.imported_links";

  void Save(const std::vector<ImportedSource>& sources);
  void RefreshCacheIfNeeded() const;
  void NotifyChanged();

  // Fired when another ImportedLinkStore instance writes the backing pref.
  void OnPrefChanged();

  // Collects |item_id| and all its descendants within |items| into |out|.
  static void CollectDescendants(const std::vector<ImportedItem>& items,
                                 const std::string& item_id,
                                 std::vector<std::string>& out);

  raw_ptr<PrefService> pref_service_;
  bool pref_available_ = false;

  // Set while this instance is writing, to suppress duplicate notification.
  bool writing_ = false;

  mutable std::vector<ImportedSource> cached_;
  mutable bool cache_dirty_ = true;

  PrefChangeRegistrar pref_registrar_;

  base::ObserverList<Observer> observers_;
};

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_IMPORTED_LINK_STORE_H_
