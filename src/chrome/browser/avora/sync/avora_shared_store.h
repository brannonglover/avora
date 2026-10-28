// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_STORE_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_STORE_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/avora/sync/avora_shared_rows.h"

class PrefService;
class PrefRegistrySimple;

namespace avora::sync {

// One row as this device's own models hold it, for SharedStore::Reconcile().
struct LocalRow {
  LocalRow();
  LocalRow(LocalRow&&);
  LocalRow& operator=(LocalRow&&);
  ~LocalRow();

  // The local model's id (Space id, sidebar item id).
  std::string local_id;
  // ContentColumns() except "position". "space_id" and "parent_id" hold
  // local ids; the store translates them.
  base::DictValue columns;
  // Rows are ordered by `index` within a container (a Space's pinned items,
  // say); a row's position is only meaningful within its container.
  std::string container;
  int index = 0;
};

// This device's copy of the account's shared rows (docs/SYNC.md), with the
// changes it has made that the server hasn't confirmed yet.
//
// - The *mirror* is what the server holds, with this device's unsent changes
//   laid over it, so diffs never re-send a change and a change never looks
//   undone while it is in flight.
// - *Links* map shared ids to local ids where the local model mints its own
//   (SpaceManager, SidebarItemStore). Unlinked ids are the same on both sides.
// - *Baselines* are the local rows as last reconciled, so a change is only
//   ever one this device made, never a remote change it hasn't applied yet.
//
// The mirror, links, unsent changes and cursor are persisted in prefs, so a
// restart doesn't need a full reload and changes made offline aren't lost.
class SharedStore {
 public:
  explicit SharedStore(PrefService* prefs);
  SharedStore(const SharedStore&) = delete;
  SharedStore& operator=(const SharedStore&) = delete;
  ~SharedStore();

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Forgets everything (sign-out, or a different account signing in).
  void Reset(std::string_view user_id);
  // The account the stored rows belong to; empty after a reset to nobody.
  const std::string& user_id() const { return user_id_; }

  // Whether a full load has completed for this account.
  bool loaded() const { return loaded_; }
  void set_loaded() { loaded_ = true; ScheduleSave(); }
  // The newest updated_at seen; null before the first load.
  std::optional<base::Time> cursor() const { return cursor_; }

  // ── Ids ──
  std::string ToShared(SharedTable table, std::string_view local_id) const;
  std::string ToLocal(SharedTable table, std::string_view shared_id) const;
  void Link(SharedTable table,
            std::string_view shared_id,
            std::string_view local_id);
  bool IsLinkedLocal(SharedTable table, std::string_view local_id) const;

  // ── The mirror ──
  const SharedRow* Find(SharedTable table, std::string_view shared_id) const;
  std::vector<const SharedRow*> Rows(SharedTable table) const;
  // Live items of `kind` in a shared Space, in position order.
  std::vector<const SharedRow*> ItemsIn(std::string_view shared_space_id,
                                        std::string_view kind) const;

  // Merges rows from PostgREST into the mirror and returns the rows whose
  // content or deletion changed, as they now stand in the mirror (unsent local
  // changes win). Rows older than the mirror's copy are ignored.
  std::vector<SharedRow> MergeFromServer(SharedTable table,
                                         const base::ListValue& rows);

  // ── Local changes ──
  // Records `columns` (shared ids, any subset of ContentColumns(), plus
  // "deleted_at") as a change to send and applies it to the mirror.
  void Write(SharedTable table,
             std::string_view shared_id,
             base::DictValue columns);
  void Delete(SharedTable table, std::string_view shared_id);

  bool HasPending() const;
  // Whether this device has a change to `shared_id` the server hasn't
  // confirmed.
  bool HasUnsentChange(SharedTable table, std::string_view shared_id) const;
  // The unsent changes for `table`, ready for sync_items/sync_spaces, each
  // stamped with `device_id`. They stay in flight until FinishPush().
  base::ListValue TakePending(SharedTable table, std::string_view device_id);
  // After the push of TakePending(table): on failure the changes go back in
  // the queue, under anything changed since.
  void FinishPush(SharedTable table, bool ok);

  // ── Projections ──
  // Diffs `rows` (everything this device holds of one kind of row, e.g. all
  // pinned items) against the baseline for `scope`, records the differences
  // as changes, and makes `rows` the new baseline. Rows that appeared get
  // created, rows that disappeared get deleted, and containers whose order
  // changed get new positions for as few rows as possible. The first call
  // for a scope only records the baseline unless `diff_first` is set (the
  // first sign-in, after links were made).
  void Reconcile(SharedTable table,
                 std::string_view scope,
                 std::vector<LocalRow> rows,
                 bool diff_first = false);
  // Makes `rows` the baseline without recording changes: after applying
  // remote rows to the local models.
  void Rebase(SharedTable table,
              std::string_view scope,
              std::vector<LocalRow> rows);

  // Writes to prefs now rather than after the usual delay.
  void SaveNow();

 private:
  struct Baseline {
    bool initialized = false;
    std::map<std::string, LocalRow> rows;
  };

  base::DictValue ToSharedColumns(SharedTable table,
                                  const base::DictValue& local_columns) const;
  void ApplyToMirror(SharedTable table,
                     std::string_view shared_id,
                     const base::DictValue& columns);
  void ScheduleSave();
  void Load();

  std::map<std::string, SharedRow>& mirror(SharedTable table) {
    return table == SharedTable::kSpaces ? spaces_ : items_;
  }
  const std::map<std::string, SharedRow>& mirror(SharedTable table) const {
    return table == SharedTable::kSpaces ? spaces_ : items_;
  }

  raw_ptr<PrefService> prefs_;
  std::string user_id_;
  bool loaded_ = false;
  std::optional<base::Time> cursor_;
  std::map<std::string, SharedRow> spaces_;
  std::map<std::string, SharedRow> items_;
  // shared id -> local id, and back.
  std::map<SharedTable, std::map<std::string, std::string>> to_local_;
  std::map<SharedTable, std::map<std::string, std::string>> to_shared_;
  // shared id -> changed columns.
  std::map<SharedTable, std::map<std::string, base::DictValue>> pending_;
  std::map<SharedTable, std::map<std::string, base::DictValue>> in_flight_;
  std::map<std::pair<SharedTable, std::string>, Baseline> baselines_;
  base::OneShotTimer save_timer_;
};

// Positions for one container: `ordered` is the container's shared ids in
// local order and `existing` the positions some of them already have. Keeps
// as many existing positions as the order allows (a longest increasing run)
// and returns new positions for the rest, between their kept neighbours.
std::map<std::string, std::string> AssignPositions(
    const std::vector<std::string>& ordered,
    const std::map<std::string, std::string>& existing);

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_STORE_H_
