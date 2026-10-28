// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_fractional_index.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace avora::sync {

namespace {

// Keys are an "integer" part, whose first character encodes its length, and
// an optional fraction that never ends in the zero digit.
constexpr std::string_view kDigits =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
constexpr char kZero = '0';
constexpr char kLast = 'z';

// The smallest integer part ("A" and 26 zeros); nothing can be decremented
// below it.
constexpr std::string_view kSmallestInteger = "A00000000000000000000000000";
static_assert(kSmallestInteger.size() == 27);

int DigitValue(char c) {
  const size_t pos = kDigits.find(c);
  return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
}

std::optional<size_t> IntegerLength(char head) {
  if (head >= 'a' && head <= 'z') {
    return static_cast<size_t>(head - 'a' + 2);
  }
  if (head >= 'A' && head <= 'Z') {
    return static_cast<size_t>('Z' - head + 2);
  }
  return std::nullopt;
}

std::optional<std::string_view> IntegerPart(std::string_view key) {
  if (key.empty()) {
    return std::nullopt;
  }
  const std::optional<size_t> length = IntegerLength(key[0]);
  if (!length || *length > key.size()) {
    return std::nullopt;
  }
  return key.substr(0, *length);
}

bool AllDigits(std::string_view s) {
  for (char c : s) {
    if (DigitValue(c) < 0) {
      return false;
    }
  }
  return true;
}

// A key strictly between fractions `a` and `b` (b null = no upper bound).
// Neither ends in the zero digit, and a < b.
std::string Midpoint(std::string_view a, std::optional<std::string_view> b) {
  if (b) {
    // Copy the shared prefix, padding `a` with zeros.
    size_t n = 0;
    while (n < b->size() && (n < a.size() ? a[n] : kZero) == (*b)[n]) {
      ++n;
    }
    if (n > 0) {
      return std::string(b->substr(0, n)) +
             Midpoint(n < a.size() ? a.substr(n) : std::string_view(),
                      b->substr(n));
    }
  }
  const int digit_a = a.empty() ? 0 : DigitValue(a[0]);
  const int digit_b =
      b ? DigitValue((*b)[0]) : static_cast<int>(kDigits.size());
  if (digit_b - digit_a > 1) {
    const int mid = static_cast<int>(std::lround(0.5 * (digit_a + digit_b)));
    return std::string(1, kDigits[mid]);
  }
  // The digits are consecutive.
  if (b && b->size() > 1) {
    return std::string(b->substr(0, 1));
  }
  return std::string(1, kDigits[digit_a]) +
         Midpoint(a.empty() ? std::string_view() : a.substr(1), std::nullopt);
}

std::optional<std::string> IncrementInteger(std::string_view x) {
  std::string digits(x.substr(1));
  char head = x[0];
  bool carry = true;
  for (int i = static_cast<int>(digits.size()) - 1; carry && i >= 0; --i) {
    const int d = DigitValue(digits[i]) + 1;
    if (d == static_cast<int>(kDigits.size())) {
      digits[i] = kZero;
    } else {
      digits[i] = kDigits[d];
      carry = false;
    }
  }
  if (!carry) {
    return std::string(1, head) + digits;
  }
  if (head == 'Z') {
    return std::string("a") + kZero;
  }
  if (head == 'z') {
    return std::nullopt;
  }
  ++head;
  if (head > 'a') {
    digits.push_back(kZero);
  } else {
    digits.pop_back();
  }
  return std::string(1, head) + digits;
}

std::optional<std::string> DecrementInteger(std::string_view x) {
  std::string digits(x.substr(1));
  char head = x[0];
  bool borrow = true;
  for (int i = static_cast<int>(digits.size()) - 1; borrow && i >= 0; --i) {
    const int d = DigitValue(digits[i]) - 1;
    if (d == -1) {
      digits[i] = kLast;
    } else {
      digits[i] = kDigits[d];
      borrow = false;
    }
  }
  if (!borrow) {
    return std::string(1, head) + digits;
  }
  if (head == 'a') {
    return std::string("Z") + kLast;
  }
  if (head == 'A') {
    return std::nullopt;
  }
  --head;
  if (head < 'Z') {
    digits.push_back(kLast);
  } else {
    digits.pop_back();
  }
  return std::string(1, head) + digits;
}

}  // namespace

bool IsValidKey(std::string_view key) {
  if (key == kSmallestInteger || !AllDigits(key.empty() ? key : key.substr(1))) {
    return false;
  }
  const std::optional<std::string_view> integer = IntegerPart(key);
  if (!integer) {
    return false;
  }
  const std::string_view fraction = key.substr(integer->size());
  return fraction.empty() || fraction.back() != kZero;
}

std::optional<std::string> KeyBetween(std::optional<std::string_view> before,
                                      std::optional<std::string_view> after) {
  if ((before && !IsValidKey(*before)) || (after && !IsValidKey(*after))) {
    return std::nullopt;
  }
  if (before && after && *before >= *after) {
    return std::nullopt;
  }

  if (!before) {
    if (!after) {
      return std::string("a") + kZero;
    }
    const std::string_view int_b = *IntegerPart(*after);
    const std::string_view frac_b = after->substr(int_b.size());
    if (int_b == kSmallestInteger) {
      return std::string(int_b) + Midpoint("", frac_b);
    }
    if (int_b < *after) {
      return std::string(int_b);
    }
    return DecrementInteger(int_b);
  }

  const std::string_view int_a = *IntegerPart(*before);
  const std::string_view frac_a = before->substr(int_a.size());
  if (!after) {
    std::optional<std::string> next = IncrementInteger(int_a);
    if (next) {
      return next;
    }
    return std::string(int_a) + Midpoint(frac_a, std::nullopt);
  }

  const std::string_view int_b = *IntegerPart(*after);
  const std::string_view frac_b = after->substr(int_b.size());
  if (int_a == int_b) {
    return std::string(int_a) + Midpoint(frac_a, frac_b);
  }
  std::optional<std::string> next = IncrementInteger(int_a);
  if (!next) {
    return std::nullopt;
  }
  if (*next < *after) {
    return next;
  }
  return std::string(int_a) + Midpoint(frac_a, std::nullopt);
}

std::vector<std::string> KeysBetween(std::optional<std::string_view> before,
                                     std::optional<std::string_view> after,
                                     size_t n) {
  std::vector<std::string> keys;
  if (n == 0) {
    return keys;
  }
  if (n == 1) {
    if (std::optional<std::string> key = KeyBetween(before, after)) {
      keys.push_back(std::move(*key));
    }
    return keys;
  }
  // Open-ended lists just count up (or down); bounded ones split in half so
  // the keys stay short.
  if (!after) {
    std::optional<std::string> key = KeyBetween(before, std::nullopt);
    for (size_t i = 0; i < n && key; ++i) {
      keys.push_back(*key);
      key = KeyBetween(keys.back(), std::nullopt);
    }
    if (keys.size() != n) {
      keys.clear();
    }
    return keys;
  }
  if (!before) {
    std::optional<std::string> key = KeyBetween(std::nullopt, after);
    for (size_t i = 0; i < n && key; ++i) {
      keys.push_back(*key);
      key = KeyBetween(std::nullopt, keys.back());
    }
    if (keys.size() != n) {
      keys.clear();
    }
    std::reverse(keys.begin(), keys.end());
    return keys;
  }
  const size_t mid = n / 2;
  std::optional<std::string> middle = KeyBetween(before, after);
  if (!middle) {
    return keys;
  }
  std::vector<std::string> low = KeysBetween(before, *middle, mid);
  std::vector<std::string> high = KeysBetween(*middle, after, n - mid - 1);
  if (low.size() != mid || high.size() != n - mid - 1) {
    return keys;
  }
  keys = std::move(low);
  keys.push_back(std::move(*middle));
  keys.insert(keys.end(), std::make_move_iterator(high.begin()),
              std::make_move_iterator(high.end()));
  return keys;
}

}  // namespace avora::sync
