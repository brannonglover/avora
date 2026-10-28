// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_ROWS_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_ROWS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

namespace avora::sync {

// The two shared tables of docs/SYNC.md (in the avoraapp repository).
enum class SharedTable { kSpaces, kItems };

// Item kinds, as stored in items.kind.
inline constexpr char kKindToday[] = "today";
inline constexpr char kKindPinned[] = "pinned";
inline constexpr char kKindFavorite[] = "favorite";
inline constexpr char kKindFolder[] = "folder";

// The columns a device writes, by table. Everything else on a row (user_id,
// created_at, updated_at, updated_by, deleted_at) is bookkeeping.
const std::vector<std::string_view>& ContentColumns(SharedTable table);

// One row as the server last reported it.
struct SharedRow {
  SharedRow();
  SharedRow(SharedRow&&);
  SharedRow& operator=(SharedRow&&);
  ~SharedRow();

  SharedRow Clone() const;

  std::string id;
  // ContentColumns(table) only; a null column is stored as a none value.
  base::DictValue columns;
  base::Time updated_at;
  std::string updated_by;
  bool deleted = false;

  // Parses a row from PostgREST. Null when the id or updated_at is missing.
  static std::optional<SharedRow> FromServer(SharedTable table,
                                             const base::DictValue& dict);

  base::DictValue ToDict() const;
  static std::optional<SharedRow> FromDict(const base::DictValue& dict);
};

// Timestamps as PostgREST writes timestamptz ("2026-10-28T16:05:01.12+00:00",
// with up to six fractional digits) and as filters accept them.
std::optional<base::Time> ParseServerTime(std::string_view text);
std::string FormatServerTime(base::Time time);

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_SHARED_ROWS_H_
