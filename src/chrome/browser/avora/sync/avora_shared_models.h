// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_MODELS_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_MODELS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/avora/avora_sidebar_item.h"
#include "chrome/browser/avora/avora_space_manager.h"
#include "chrome/browser/avora/sync/avora_shared_store.h"

class PrefService;

namespace avora::sync {

// The shared rows that live in Avora's own models: Spaces (SpaceManager) and
// each Space's Favorites and pinned items (SidebarItemStore). Today tabs live
// in the tab strips instead and are handled by TabSyncService directly.
//
// Projecting turns the models into LocalRows for SharedStore to diff;
// applying writes changed server rows back into the models. Both models mint
// their own ids, so rows created from the server are linked to theirs.
//
// Pinned folders aren't shared yet: pinned items sync flat, and an item filed
// in a folder here stays filed.
class SharedModels : public SpaceManagerObserver,
                     public SidebarItemStore::Observer {
 public:
  // The projection scopes, for SharedStore::Reconcile().
  static constexpr char kSpacesScope[] = "spaces";
  static constexpr char kSidebarScope[] = "sidebar";

  // Removes a pinned item, unmarking its open tab if it has one. Supplied by
  // the views layer, which owns the tab markers; without it the item is only
  // removed from the store.
  using PinnedItemRemover =
      base::RepeatingCallback<void(const std::string& item_id)>;

  // `on_changed` runs whenever the models change, including through this
  // class's own Apply().
  SharedModels(PrefService* prefs, base::RepeatingClosure on_changed);
  SharedModels(const SharedModels&) = delete;
  SharedModels& operator=(const SharedModels&) = delete;
  ~SharedModels() override;

  void set_pinned_item_remover(PinnedItemRemover remover) {
    pinned_item_remover_ = std::move(remover);
  }

  std::vector<LocalRow> ProjectSpaces() const;
  std::vector<LocalRow> ProjectSidebar() const;

  // First sign-in: links local Spaces to account Spaces of the same name, and
  // local Favorites and pinned items to account items of the same kind and
  // URL in the linked Space.
  void LinkMatching(SharedStore& store) const;

  // Applies changed server rows: Spaces first, then Favorites and pinned
  // items. Rows of other kinds are ignored.
  void Apply(SharedStore& store,
             const std::vector<SharedRow>& spaces,
             const std::vector<SharedRow>& items);

  // The local id of a Space that exists here, or empty.
  std::string LocalSpaceId(const SharedStore& store,
                           const std::string& shared_space_id) const;

  SpaceManager& space_manager() { return space_manager_; }

 private:
  // SpaceManagerObserver:
  void OnSpacesChanged() override;
  // SidebarItemStore::Observer:
  void OnSidebarItemsChanged() override;

  void ApplySpaces(SharedStore& store, const std::vector<SharedRow>& rows);
  void ApplyItems(SharedStore& store, const std::vector<SharedRow>& rows);
  void RemoveItem(const SidebarItem& item);
  void RemoveSpace(const std::string& local_id);

  raw_ptr<PrefService> prefs_;
  base::RepeatingClosure on_changed_;
  PinnedItemRemover pinned_item_remover_;
  SpaceManager space_manager_;
  SidebarItemStore item_store_;
  base::ScopedObservation<SpaceManager, SpaceManagerObserver>
      space_observation_{this};
  base::ScopedObservation<SidebarItemStore, SidebarItemStore::Observer>
      item_observation_{this};
};

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_MODELS_H_
