// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_FRACTIONAL_INDEX_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_FRACTIONAL_INDEX_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avora::sync {

// Order keys for shared rows (the `position` column in docs/SYNC.md): strings
// that sort bytewise, where a new key can always be made between two others,
// so devices can insert and move items without renumbering anyone else's.
//
// This is the base-62 scheme of the `fractional-indexing` npm package the
// phone uses, and must produce the same keys: generateKeyBetween(null, null)
// is "a0", after "a0" comes "a1", and between them "a0V".

// A key strictly between `before` and `after`. Either may be null, meaning
// the start or the end of the list. Null when an argument isn't a valid key
// or `before` doesn't sort below `after`.
std::optional<std::string> KeyBetween(std::optional<std::string_view> before,
                                      std::optional<std::string_view> after);

// `n` ascending keys between `before` and `after`, spread out rather than
// crowded at one end. Empty when KeyBetween() would fail.
std::vector<std::string> KeysBetween(std::optional<std::string_view> before,
                                     std::optional<std::string_view> after,
                                     size_t n);

// Whether `key` is a key KeyBetween() could have produced.
bool IsValidKey(std::string_view key);

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_FRACTIONAL_INDEX_H_
