// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_shared_rows.h"

#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"

namespace avora::sync {

namespace {

bool ParseDigits(std::string_view text, size_t pos, size_t count, int* out) {
  if (pos + count > text.size()) {
    return false;
  }
  int value = 0;
  for (size_t i = pos; i < pos + count; ++i) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
    value = value * 10 + (text[i] - '0');
  }
  *out = value;
  return true;
}

}  // namespace

const std::vector<std::string_view>& ContentColumns(SharedTable table) {
  static const base::NoDestructor<std::vector<std::string_view>> kSpaces(
      std::vector<std::string_view>{"name", "icon", "color", "position"});
  static const base::NoDestructor<std::vector<std::string_view>> kItems(
      std::vector<std::string_view>{
          "space_id", "kind", "parent_id", "url", "title", "favicon_url",
          "preview_path", "preview_updated_at", "position", "last_active_at"});
  return table == SharedTable::kSpaces ? *kSpaces : *kItems;
}

SharedRow::SharedRow() = default;
SharedRow::SharedRow(SharedRow&&) = default;
SharedRow& SharedRow::operator=(SharedRow&&) = default;
SharedRow::~SharedRow() = default;

SharedRow SharedRow::Clone() const {
  SharedRow copy;
  copy.id = id;
  copy.columns = columns.Clone();
  copy.updated_at = updated_at;
  copy.updated_by = updated_by;
  copy.deleted = deleted;
  return copy;
}

// static
std::optional<SharedRow> SharedRow::FromServer(SharedTable table,
                                               const base::DictValue& dict) {
  const std::string* id = dict.FindString("id");
  const std::string* updated_at = dict.FindString("updated_at");
  if (!id || id->empty() || !updated_at) {
    return std::nullopt;
  }
  std::optional<base::Time> time = ParseServerTime(*updated_at);
  if (!time) {
    return std::nullopt;
  }
  SharedRow row;
  row.id = *id;
  row.updated_at = *time;
  if (const std::string* by = dict.FindString("updated_by")) {
    row.updated_by = *by;
  }
  const base::Value* deleted_at = dict.Find("deleted_at");
  row.deleted = deleted_at && !deleted_at->is_none();
  for (std::string_view column : ContentColumns(table)) {
    const base::Value* value = dict.Find(column);
    row.columns.Set(column, value ? value->Clone() : base::Value());
  }
  return row;
}

base::DictValue SharedRow::ToDict() const {
  base::DictValue dict;
  dict.Set("id", id);
  dict.Set("columns", columns.Clone());
  dict.Set("updated_at", FormatServerTime(updated_at));
  dict.Set("updated_by", updated_by);
  dict.Set("deleted", deleted);
  return dict;
}

// static
std::optional<SharedRow> SharedRow::FromDict(const base::DictValue& dict) {
  const std::string* id = dict.FindString("id");
  const base::DictValue* columns = dict.FindDict("columns");
  const std::string* updated_at = dict.FindString("updated_at");
  if (!id || !columns || !updated_at) {
    return std::nullopt;
  }
  std::optional<base::Time> time = ParseServerTime(*updated_at);
  if (!time) {
    return std::nullopt;
  }
  SharedRow row;
  row.id = *id;
  row.columns = columns->Clone();
  row.updated_at = *time;
  if (const std::string* by = dict.FindString("updated_by")) {
    row.updated_by = *by;
  }
  row.deleted = dict.FindBool("deleted").value_or(false);
  return row;
}

std::optional<base::Time> ParseServerTime(std::string_view text) {
  // YYYY-MM-DDTHH:MM:SS[.f{1,6}](Z|±HH[:MM])
  base::Time::Exploded exploded = {};
  if (text.size() < 19 || text[4] != '-' || text[7] != '-' ||
      (text[10] != 'T' && text[10] != ' ') || text[13] != ':' ||
      text[16] != ':' || !ParseDigits(text, 0, 4, &exploded.year) ||
      !ParseDigits(text, 5, 2, &exploded.month) ||
      !ParseDigits(text, 8, 2, &exploded.day_of_month) ||
      !ParseDigits(text, 11, 2, &exploded.hour) ||
      !ParseDigits(text, 14, 2, &exploded.minute) ||
      !ParseDigits(text, 17, 2, &exploded.second)) {
    return std::nullopt;
  }
  size_t pos = 19;
  int64_t micros = 0;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    int digits = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      if (digits < 6) {
        micros = micros * 10 + (text[pos] - '0');
        ++digits;
      }
      ++pos;
    }
    if (digits == 0) {
      return std::nullopt;
    }
    for (; digits < 6; ++digits) {
      micros *= 10;
    }
  }
  int offset_minutes = 0;
  if (pos < text.size() && text[pos] == 'Z') {
    ++pos;
  } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    const int sign = text[pos] == '-' ? -1 : 1;
    int hours = 0;
    int minutes = 0;
    if (!ParseDigits(text, pos + 1, 2, &hours)) {
      return std::nullopt;
    }
    pos += 3;
    if (pos < text.size() && text[pos] == ':') {
      ++pos;
    }
    if (pos < text.size()) {
      if (!ParseDigits(text, pos, 2, &minutes)) {
        return std::nullopt;
      }
      pos += 2;
    }
    offset_minutes = sign * (hours * 60 + minutes);
  } else {
    return std::nullopt;
  }
  if (pos != text.size()) {
    return std::nullopt;
  }
  base::Time time;
  if (!base::Time::FromUTCExploded(exploded, &time)) {
    return std::nullopt;
  }
  return time + base::Microseconds(micros) - base::Minutes(offset_minutes);
}

std::string FormatServerTime(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);
  const int64_t micros =
      (time - base::Time::UnixEpoch()).InMicroseconds() % 1000000;
  return base::StringPrintf("%04d-%02d-%02dT%02d:%02d:%02d.%06lld+00:00",
                            exploded.year, exploded.month,
                            exploded.day_of_month, exploded.hour,
                            exploded.minute, exploded.second,
                            static_cast<long long>(micros < 0 ? micros + 1000000
                                                              : micros));
}

}  // namespace avora::sync
