// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_fractional_index.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace avora::sync {
namespace {

std::string Between(std::optional<std::string_view> a,
                    std::optional<std::string_view> b) {
  return KeyBetween(a, b).value_or("<invalid>");
}

// The expected keys are the `fractional-indexing` package's own test cases,
// so the phone and the desktop agree on every key.
TEST(AvoraFractionalIndexTest, MatchesThePhonesKeys) {
  EXPECT_EQ("a0", Between(std::nullopt, std::nullopt));
  EXPECT_EQ("a1", Between(std::nullopt, "a2"));
  EXPECT_EQ("Zz", Between(std::nullopt, "a0"));
  EXPECT_EQ("Zy", Between(std::nullopt, "Zz"));
  EXPECT_EQ("a1", Between("a0", std::nullopt));
  EXPECT_EQ("a2", Between("a1", std::nullopt));
  EXPECT_EQ("a0V", Between("a0", "a1"));
  EXPECT_EQ("a1V", Between("a1", "a2"));
  EXPECT_EQ("a0l", Between("a0V", "a1"));
  EXPECT_EQ("ZzV", Between("Zz", "a0"));
  EXPECT_EQ("a0", Between("Zz", "a1"));
  EXPECT_EQ("Xzzz", Between(std::nullopt, "Y00"));
  EXPECT_EQ("c000", Between("bzz", std::nullopt));
  EXPECT_EQ("a0G", Between("a0", "a0V"));
  EXPECT_EQ("a08", Between("a0", "a0G"));
  EXPECT_EQ("b127", Between("b125", "b129"));
  EXPECT_EQ("a1", Between("a0", "a1V"));
  EXPECT_EQ("a0", Between("Zz", "a01"));
  EXPECT_EQ("a0", Between(std::nullopt, "a0V"));
  EXPECT_EQ("b99", Between(std::nullopt, "b999"));
  EXPECT_EQ("A000000000000000000000000000V",
            Between(std::nullopt, "A000000000000000000000000001"));
  EXPECT_EQ("zzzzzzzzzzzzzzzzzzzzzzzzzzzV",
            Between("zzzzzzzzzzzzzzzzzzzzzzzzzzz", std::nullopt));
}

TEST(AvoraFractionalIndexTest, RejectsInvalidKeys) {
  EXPECT_FALSE(KeyBetween(std::nullopt, "A00000000000000000000000000"));
  EXPECT_FALSE(KeyBetween("a00", std::nullopt));  // Trailing zero.
  EXPECT_FALSE(KeyBetween("a0", "a0"));
  EXPECT_FALSE(KeyBetween("a1", "a0"));
  EXPECT_FALSE(KeyBetween("0", "1"));
  EXPECT_FALSE(KeyBetween("a", std::nullopt));  // Integer part too short.
  EXPECT_FALSE(KeyBetween("a0!", std::nullopt));
  EXPECT_FALSE(IsValidKey(""));
  EXPECT_TRUE(IsValidKey("a0V"));
}

TEST(AvoraFractionalIndexTest, ManyKeysAreAscendingAndBetweenTheBounds) {
  for (const auto& [lo, hi] :
       std::vector<std::pair<std::optional<std::string>,
                             std::optional<std::string>>>{
           {std::nullopt, std::nullopt},
           {"a0", std::nullopt},
           {std::nullopt, "a0"},
           {"a0", "a1"},
           {"a0V", "a0W"}}) {
    const std::vector<std::string> keys = KeysBetween(lo, hi, 25);
    ASSERT_EQ(25u, keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
      EXPECT_TRUE(IsValidKey(keys[i])) << keys[i];
      if (i > 0) {
        EXPECT_LT(keys[i - 1], keys[i]);
      }
    }
    if (lo) {
      EXPECT_LT(*lo, keys.front());
    }
    if (hi) {
      EXPECT_LT(keys.back(), *hi);
    }
  }
  EXPECT_EQ((std::vector<std::string>{"a0", "a1", "a2"}),
            KeysBetween(std::nullopt, std::nullopt, 3));
}

TEST(AvoraFractionalIndexTest, RepeatedInsertsStayOrdered) {
  // Always inserting right after the first item, as dragging a tab to the
  // top of a list does.
  std::string first = *KeyBetween(std::nullopt, std::nullopt);
  std::string next = *KeyBetween(first, std::nullopt);
  for (int i = 0; i < 200; ++i) {
    std::optional<std::string> key = KeyBetween(first, next);
    ASSERT_TRUE(key);
    EXPECT_LT(first, *key);
    EXPECT_LT(*key, next);
    next = *key;
  }
}

}  // namespace
}  // namespace avora::sync
