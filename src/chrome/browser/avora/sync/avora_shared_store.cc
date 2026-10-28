// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_shared_store.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/avora/sync/avora_fractional_index.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace avora::sync {

namespace {

constexpr char kPrefName[] = "avora.sync.shared";
constexpr char kDeletedAt[] = "deleted_at";
constexpr char kPosition[] = "position";
// Columns holding ids of other shared rows.
constexpr char kSpaceId[] = "space_id";
constexpr char kParentId[] = "parent_id";

// Batching writes keeps a burst of tab changes from reserializing the whole
// mirror for each one.
constexpr base::TimeDelta kSaveDelay = base::Seconds(2);

const char* TableKey(SharedTable table) {
  return table == SharedTable::kSpaces ? "spaces" : "items";
}

constexpr SharedTable kTables[] = {SharedTable::kSpaces, SharedTable::kItems};

// Lays `changes` over `row`'s content and deletion.
void Overlay(SharedRow& row, const base::DictValue& changes) {
  for (const auto [key, value] : changes) {
    if (key == kDeletedAt) {
      row.deleted = !value.is_none();
    } else {
      row.columns.Set(key, value.Clone());
    }
  }
}

bool SameContent(const SharedRow& a, const SharedRow& b) {
  return a.deleted == b.deleted && a.columns == b.columns;
}

// Indices into `values` of a longest strictly increasing subsequence.
std::vector<size_t> LongestIncreasingRun(
    const std::vector<std::string>& values) {
  std::vector<size_t> tails;  // Index of the smallest tail of each length.
  std::vector<size_t> prev(values.size(), values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    const auto it = std::lower_bound(
        tails.begin(), tails.end(), i,
        [&](size_t a, size_t b) { return values[a] < values[b]; });
    if (it != tails.begin()) {
      prev[i] = *(it - 1);
    }
    if (it == tails.end()) {
      tails.push_back(i);
    } else {
      *it = i;
    }
  }
  std::vector<size_t> run;
  for (size_t i = tails.empty() ? values.size() : tails.back();
       i < values.size(); i = prev[i]) {
    run.push_back(i);
  }
  std::reverse(run.begin(), run.end());
  return run;
}

}  // namespace

LocalRow::LocalRow() = default;
LocalRow::LocalRow(LocalRow&&) = default;
LocalRow& LocalRow::operator=(LocalRow&&) = default;
LocalRow::~LocalRow() = default;

std::map<std::string, std::string> AssignPositions(
    const std::vector<std::string>& ordered,
    const std::map<std::string, std::string>& existing) {
  // Candidates for keeping: rows with a valid position, in local order.
  std::vector<size_t> candidates;
  std::vector<std::string> positions;
  for (size_t i = 0; i < ordered.size(); ++i) {
    const auto it = existing.find(ordered[i]);
    if (it != existing.end() && IsValidKey(it->second)) {
      candidates.push_back(i);
      positions.push_back(it->second);
    }
  }
  std::vector<bool> kept(ordered.size(), false);
  for (size_t run_index : LongestIncreasingRun(positions)) {
    kept[candidates[run_index]] = true;
  }

  std::map<std::string, std::string> assigned;
  std::optional<std::string> before;
  size_t i = 0;
  while (i < ordered.size()) {
    if (kept[i]) {
      before = existing.at(ordered[i]);
      ++i;
      continue;
    }
    size_t end = i;
    while (end < ordered.size() && !kept[end]) {
      ++end;
    }
    std::optional<std::string> after;
    if (end < ordered.size()) {
      after = existing.at(ordered[end]);
    }
    std::vector<std::string> keys = KeysBetween(
        before ? std::optional<std::string_view>(*before) : std::nullopt,
        after ? std::optional<std::string_view>(*after) : std::nullopt,
        end - i);
    if (!keys.empty()) {
      before = keys.back();
    }
    for (size_t k = 0; k < keys.size(); ++k) {
      assigned[ordered[i + k]] = std::move(keys[k]);
    }
    i = end;
  }
  return assigned;
}

SharedStore::SharedStore(PrefService* prefs) : prefs_(prefs) {
  Load();
}

SharedStore::~SharedStore() {
  if (save_timer_.IsRunning()) {
    SaveNow();
  }
}

// static
void SharedStore::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kPrefName);
}

void SharedStore::Reset(std::string_view user_id) {
  user_id_ = std::string(user_id);
  loaded_ = false;
  cursor_.reset();
  spaces_.clear();
  items_.clear();
  to_local_.clear();
  to_shared_.clear();
  pending_.clear();
  in_flight_.clear();
  baselines_.clear();
  SaveNow();
}

std::string SharedStore::ToShared(SharedTable table,
                                  std::string_view local_id) const {
  const auto table_it = to_shared_.find(table);
  if (table_it != to_shared_.end()) {
    const auto it = table_it->second.find(std::string(local_id));
    if (it != table_it->second.end()) {
      return it->second;
    }
  }
  return std::string(local_id);
}

std::string SharedStore::ToLocal(SharedTable table,
                                 std::string_view shared_id) const {
  const auto table_it = to_local_.find(table);
  if (table_it != to_local_.end()) {
    const auto it = table_it->second.find(std::string(shared_id));
    if (it != table_it->second.end()) {
      return it->second;
    }
  }
  return std::string(shared_id);
}

void SharedStore::Link(SharedTable table,
                       std::string_view shared_id,
                       std::string_view local_id) {
  // Drop stale links on either side first, so the maps stay one-to-one.
  auto& to_local = to_local_[table];
  auto& to_shared = to_shared_[table];
  if (const auto it = to_local.find(std::string(shared_id));
      it != to_local.end()) {
    to_shared.erase(it->second);
  }
  if (const auto it = to_shared.find(std::string(local_id));
      it != to_shared.end()) {
    to_local.erase(it->second);
  }
  if (shared_id == local_id) {
    to_local.erase(std::string(shared_id));
    to_shared.erase(std::string(local_id));
  } else {
    to_local[std::string(shared_id)] = std::string(local_id);
    to_shared[std::string(local_id)] = std::string(shared_id);
  }
  ScheduleSave();
}

bool SharedStore::IsLinkedLocal(SharedTable table,
                                std::string_view local_id) const {
  const auto it = to_shared_.find(table);
  return it != to_shared_.end() && it->second.count(std::string(local_id));
}

const SharedRow* SharedStore::Find(SharedTable table,
                                   std::string_view shared_id) const {
  const auto& rows = mirror(table);
  const auto it = rows.find(std::string(shared_id));
  return it == rows.end() ? nullptr : &it->second;
}

std::vector<const SharedRow*> SharedStore::Rows(SharedTable table) const {
  std::vector<const SharedRow*> rows;
  for (const auto& [id, row] : mirror(table)) {
    rows.push_back(&row);
  }
  return rows;
}

std::vector<const SharedRow*> SharedStore::ItemsIn(
    std::string_view shared_space_id,
    std::string_view kind) const {
  std::vector<const SharedRow*> rows;
  for (const auto& [id, row] : items_) {
    const std::string* space = row.columns.FindString(kSpaceId);
    const std::string* row_kind = row.columns.FindString("kind");
    if (!row.deleted && space && *space == shared_space_id && row_kind &&
        *row_kind == kind) {
      rows.push_back(&row);
    }
  }
  std::sort(rows.begin(), rows.end(), [](const SharedRow* a, const SharedRow* b) {
    const std::string* pa = a->columns.FindString(kPosition);
    const std::string* pb = b->columns.FindString(kPosition);
    const std::string_view sa = pa ? std::string_view(*pa) : std::string_view();
    const std::string_view sb = pb ? std::string_view(*pb) : std::string_view();
    return sa != sb ? sa < sb : a->id < b->id;
  });
  return rows;
}

std::vector<SharedRow> SharedStore::MergeFromServer(
    SharedTable table,
    const base::ListValue& rows) {
  std::vector<SharedRow> changed;
  auto& stored = mirror(table);
  for (const base::Value& value : rows) {
    if (!value.is_dict()) {
      continue;
    }
    std::optional<SharedRow> row = SharedRow::FromServer(table, value.GetDict());
    if (!row) {
      continue;
    }
    if (!cursor_ || row->updated_at > *cursor_) {
      cursor_ = row->updated_at;
    }
    const auto existing = stored.find(row->id);
    if (existing != stored.end() &&
        !existing->second.updated_at.is_null() &&
        row->updated_at < existing->second.updated_at) {
      // Already seen something newer; the catch-up overlap returns old rows.
      continue;
    }
    // Unsent changes win until they are sent.
    if (const auto it = in_flight_[table].find(row->id);
        it != in_flight_[table].end()) {
      Overlay(*row, it->second);
    }
    if (const auto it = pending_[table].find(row->id);
        it != pending_[table].end()) {
      Overlay(*row, it->second);
    }
    const bool is_change =
        existing == stored.end() || !SameContent(existing->second, *row);
    if (is_change) {
      changed.push_back(row->Clone());
    }
    stored[row->id] = std::move(*row);
  }
  ScheduleSave();
  return changed;
}

void SharedStore::Write(SharedTable table,
                        std::string_view shared_id,
                        base::DictValue columns) {
  if (columns.empty()) {
    return;
  }
  ApplyToMirror(table, shared_id, columns);
  base::DictValue& pending = pending_[table][std::string(shared_id)];
  pending.Merge(std::move(columns));
  ScheduleSave();
}

void SharedStore::Delete(SharedTable table, std::string_view shared_id) {
  const SharedRow* row = Find(table, shared_id);
  if (!row || row->deleted) {
    return;
  }
  base::DictValue columns;
  // Any timestamp; the server stamps its own.
  columns.Set(kDeletedAt, FormatServerTime(base::Time::Now()));
  Write(table, shared_id, std::move(columns));
}

bool SharedStore::HasPending() const {
  for (const auto& [table, rows] : pending_) {
    if (!rows.empty()) {
      return true;
    }
  }
  return false;
}

base::ListValue SharedStore::TakePending(SharedTable table,
                                         std::string_view device_id) {
  base::ListValue changes;
  auto& pending = pending_[table];
  auto& in_flight = in_flight_[table];
  for (auto& [id, columns] : pending) {
    base::DictValue change = columns.Clone();
    change.Set("id", id);
    change.Set("updated_by", device_id);
    changes.Append(std::move(change));
    in_flight[id].Merge(std::move(columns));
  }
  pending.clear();
  return changes;
}

void SharedStore::FinishPush(SharedTable table, bool ok) {
  auto& in_flight = in_flight_[table];
  if (!ok) {
    auto& pending = pending_[table];
    for (auto& [id, columns] : in_flight) {
      // Anything changed since is newer than what failed.
      base::DictValue merged = std::move(columns);
      if (const auto it = pending.find(id); it != pending.end()) {
        merged.Merge(std::move(it->second));
      }
      pending[id] = std::move(merged);
    }
  }
  in_flight.clear();
  ScheduleSave();
}

base::DictValue SharedStore::ToSharedColumns(
    SharedTable table,
    const base::DictValue& local_columns) const {
  base::DictValue shared = local_columns.Clone();
  if (table != SharedTable::kItems) {
    return shared;
  }
  if (const std::string* space = local_columns.FindString(kSpaceId)) {
    shared.Set(kSpaceId, ToShared(SharedTable::kSpaces, *space));
  }
  if (const std::string* parent = local_columns.FindString(kParentId)) {
    shared.Set(kParentId, ToShared(SharedTable::kItems, *parent));
  }
  return shared;
}

void SharedStore::Reconcile(SharedTable table,
                            std::string_view scope,
                            std::vector<LocalRow> rows,
                            bool diff_first) {
  Baseline& baseline = baselines_[{table, std::string(scope)}];
  if (!baseline.initialized && !diff_first) {
    Rebase(table, scope, std::move(rows));
    return;
  }

  std::map<std::string, LocalRow> current;
  for (LocalRow& row : rows) {
    current[row.local_id] = std::move(row);
  }

  // Containers whose rows need positions looked at, and the rows that can't
  // keep the position they have (new here, or moved between containers).
  std::set<std::string> touched_containers;
  std::set<std::string> needs_position;

  for (const auto& [local_id, row] : current) {
    const std::string shared_id = ToShared(table, local_id);
    base::DictValue shared_columns = ToSharedColumns(table, row.columns);
    const SharedRow* mirrored = Find(table, shared_id);
    const auto before = baseline.rows.find(local_id);

    base::DictValue changes;
    if (before == baseline.rows.end()) {
      // New here.
      if (!mirrored || mirrored->deleted) {
        changes = std::move(shared_columns);
        if (mirrored && mirrored->deleted) {
          changes.Set(kDeletedAt, base::Value());
        }
        needs_position.insert(shared_id);
      } else {
        for (const auto [key, value] : shared_columns) {
          const base::Value* server = mirrored->columns.Find(key);
          if (!server || *server != value) {
            changes.Set(key, value.Clone());
          }
        }
      }
      touched_containers.insert(row.container);
    } else {
      const base::DictValue before_shared =
          ToSharedColumns(table, before->second.columns);
      for (const auto [key, value] : shared_columns) {
        const base::Value* old_value = before_shared.Find(key);
        const base::Value* server =
            mirrored ? mirrored->columns.Find(key) : nullptr;
        if ((!old_value || *old_value != value) &&
            (!server || *server != value)) {
          changes.Set(key, value.Clone());
        }
      }
      if (before->second.container != row.container) {
        needs_position.insert(shared_id);
        touched_containers.insert(row.container);
      }
      if (!mirrored) {
        // The mirror lost it (a reset); send it whole.
        changes = std::move(shared_columns);
        needs_position.insert(shared_id);
        touched_containers.insert(row.container);
      }
    }
    Write(table, shared_id, std::move(changes));
  }

  for (const auto& [local_id, row] : baseline.rows) {
    if (!current.count(local_id)) {
      Delete(table, ToShared(table, local_id));
    }
  }

  // Order: a container whose sequence of rows changed gets positions.
  std::map<std::string, std::vector<const LocalRow*>> now_by_container;
  std::map<std::string, std::vector<std::string>> before_by_container;
  for (const auto& [local_id, row] : current) {
    now_by_container[row.container].push_back(&row);
  }
  std::map<std::string, std::vector<const LocalRow*>> before_rows;
  for (const auto& [local_id, row] : baseline.rows) {
    before_rows[row.container].push_back(&row);
  }
  const auto by_index = [](const LocalRow* a, const LocalRow* b) {
    return a->index < b->index;
  };
  for (auto& [container, list] : before_rows) {
    std::sort(list.begin(), list.end(), by_index);
    for (const LocalRow* row : list) {
      if (current.count(row->local_id) &&
          current.at(row->local_id).container == container) {
        before_by_container[container].push_back(row->local_id);
      }
    }
  }
  for (auto& [container, list] : now_by_container) {
    std::sort(list.begin(), list.end(), by_index);
    std::vector<std::string> local_order;
    for (const LocalRow* row : list) {
      if (baseline.rows.count(row->local_id) &&
          baseline.rows.at(row->local_id).container == container) {
        local_order.push_back(row->local_id);
      }
    }
    if (!touched_containers.count(container) &&
        local_order == before_by_container[container]) {
      continue;
    }
    std::vector<std::string> ordered;
    std::map<std::string, std::string> existing;
    for (const LocalRow* row : list) {
      const std::string shared_id = ToShared(table, row->local_id);
      ordered.push_back(shared_id);
      const SharedRow* mirrored = Find(table, shared_id);
      const std::string* position =
          mirrored ? mirrored->columns.FindString(kPosition) : nullptr;
      if (position && !needs_position.count(shared_id)) {
        existing[shared_id] = *position;
      }
    }
    for (auto& [shared_id, position] : AssignPositions(ordered, existing)) {
      base::DictValue change;
      change.Set(kPosition, position);
      Write(table, shared_id, std::move(change));
    }
  }

  baseline.initialized = true;
  baseline.rows = std::move(current);
}

void SharedStore::Rebase(SharedTable table,
                         std::string_view scope,
                         std::vector<LocalRow> rows) {
  Baseline& baseline = baselines_[{table, std::string(scope)}];
  baseline.initialized = true;
  baseline.rows.clear();
  for (LocalRow& row : rows) {
    std::string id = row.local_id;
    baseline.rows[std::move(id)] = std::move(row);
  }
}

void SharedStore::ApplyToMirror(SharedTable table,
                                std::string_view shared_id,
                                const base::DictValue& columns) {
  auto& stored = mirror(table);
  auto it = stored.find(std::string(shared_id));
  if (it == stored.end()) {
    SharedRow row;
    row.id = std::string(shared_id);
    for (std::string_view column : ContentColumns(table)) {
      row.columns.Set(column, base::Value());
    }
    it = stored.emplace(row.id, std::move(row)).first;
  }
  Overlay(it->second, columns);
}

void SharedStore::ScheduleSave() {
  if (!prefs_) {
    return;
  }
  save_timer_.Start(FROM_HERE, kSaveDelay,
                    base::BindOnce(&SharedStore::SaveNow,
                                   base::Unretained(this)));
}

void SharedStore::SaveNow() {
  save_timer_.Stop();
  if (!prefs_) {
    return;
  }
  base::DictValue state;
  state.Set("user_id", user_id_);
  state.Set("loaded", loaded_);
  if (cursor_) {
    state.Set("cursor", FormatServerTime(*cursor_));
  }
  base::DictValue links;
  base::DictValue pending;
  for (SharedTable table : kTables) {
    base::DictValue rows;
    for (const auto& [id, row] : mirror(table)) {
      rows.Set(id, row.ToDict());
    }
    state.Set(TableKey(table), std::move(rows));

    base::DictValue table_links;
    for (const auto& [shared, local] : to_local_[table]) {
      table_links.Set(shared, local);
    }
    links.Set(TableKey(table), std::move(table_links));

    // In-flight changes are saved as unsent: if Avora quits before the
    // server answers, sending them again is harmless.
    base::DictValue table_pending;
    for (const auto& [id, columns] : in_flight_[table]) {
      table_pending.Set(id, columns.Clone());
    }
    for (const auto& [id, columns] : pending_[table]) {
      base::DictValue* merged = table_pending.EnsureDict(id);
      merged->Merge(columns.Clone());
    }
    pending.Set(TableKey(table), std::move(table_pending));
  }
  state.Set("links", std::move(links));
  state.Set("pending", std::move(pending));
  prefs_->SetDict(kPrefName, std::move(state));
}

void SharedStore::Load() {
  if (!prefs_) {
    return;
  }
  const base::DictValue& state = prefs_->GetDict(kPrefName);
  if (const std::string* user = state.FindString("user_id")) {
    user_id_ = *user;
  }
  loaded_ = state.FindBool("loaded").value_or(false);
  if (const std::string* cursor = state.FindString("cursor")) {
    cursor_ = ParseServerTime(*cursor);
  }
  const base::DictValue* links = state.FindDict("links");
  const base::DictValue* pending = state.FindDict("pending");
  for (SharedTable table : kTables) {
    if (const base::DictValue* rows = state.FindDict(TableKey(table))) {
      for (const auto [id, value] : *rows) {
        if (!value.is_dict()) {
          continue;
        }
        if (std::optional<SharedRow> row = SharedRow::FromDict(value.GetDict())) {
          mirror(table)[id] = std::move(*row);
        }
      }
    }
    if (const base::DictValue* table_links =
            links ? links->FindDict(TableKey(table)) : nullptr) {
      for (const auto [shared, local] : *table_links) {
        if (local.is_string()) {
          to_local_[table][shared] = local.GetString();
          to_shared_[table][local.GetString()] = shared;
        }
      }
    }
    if (const base::DictValue* table_pending =
            pending ? pending->FindDict(TableKey(table)) : nullptr) {
      for (const auto [id, columns] : *table_pending) {
        if (columns.is_dict()) {
          pending_[table][id] = columns.GetDict().Clone();
        }
      }
    }
  }
}

}  // namespace avora::sync
