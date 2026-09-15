// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_imported_links.h"

#include "base/json/values_util.h"

namespace avora {

namespace {

int ItemTypeToInt(ImportedItemType type) {
  return static_cast<int>(type);
}

ImportedItemType IntToItemType(int value) {
  switch (value) {
    case 0:
      return ImportedItemType::kFolder;
    case 1:
      return ImportedItemType::kLink;
    default:
      return ImportedItemType::kLink;
  }
}

}  // namespace

// ── ImportedItem ────────────────────────────────────────────────────────────

base::DictValue ImportedItem::ToDict() const {
  return base::DictValue()
      .Set("id", id)
      .Set("parent_id", parent_id)
      .Set("type", ItemTypeToInt(type))
      .Set("title", title)
      .Set("url", url)
      .Set("order", order);
}

ImportedItem ImportedItem::FromDict(const base::DictValue& dict) {
  ImportedItem item;
  if (const std::string* val = dict.FindString("id")) {
    item.id = *val;
  }
  if (const std::string* val = dict.FindString("parent_id")) {
    item.parent_id = *val;
  }
  if (std::optional<int> val = dict.FindInt("type")) {
    item.type = IntToItemType(*val);
  }
  if (const std::string* val = dict.FindString("title")) {
    item.title = *val;
  }
  if (const std::string* val = dict.FindString("url")) {
    item.url = *val;
  }
  if (std::optional<int> val = dict.FindInt("order")) {
    item.order = *val;
  }
  return item;
}

// ── ImportedSource ──────────────────────────────────────────────────────────

base::DictValue ImportedSource::ToDict() const {
  base::ListValue items_list;
  for (const auto& item : items) {
    items_list.Append(item.ToDict());
  }
  return base::DictValue()
      .Set("id", id)
      .Set("space_id", space_id)
      .Set("browser", browser)
      .Set("profile_name", profile_name)
      .Set("imported_at", base::TimeToValue(imported_at))
      .Set("items", std::move(items_list));
}

ImportedSource ImportedSource::FromDict(const base::DictValue& dict) {
  ImportedSource source;
  if (const std::string* val = dict.FindString("id")) {
    source.id = *val;
  }
  if (const std::string* val = dict.FindString("space_id")) {
    source.space_id = *val;
  }
  if (const std::string* val = dict.FindString("browser")) {
    source.browser = *val;
  }
  if (const std::string* val = dict.FindString("profile_name")) {
    source.profile_name = *val;
  }
  if (const base::Value* val = dict.Find("imported_at")) {
    if (std::optional<base::Time> t = base::ValueToTime(*val)) {
      source.imported_at = *t;
    }
  }
  if (const base::ListValue* items_list = dict.FindList("items")) {
    for (const auto& item_val : *items_list) {
      if (item_val.is_dict()) {
        source.items.push_back(ImportedItem::FromDict(item_val.GetDict()));
      }
    }
  }
  return source;
}

}  // namespace avora
