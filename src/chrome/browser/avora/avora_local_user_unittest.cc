// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_local_user.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace avora {
namespace {

TEST(AvoraLocalUserTest, InitialsTakeFirstAndLastWords) {
  EXPECT_EQ(u"BG", InitialsFor(u"Brannon Glover"));
  EXPECT_EQ(u"MW", InitialsFor(u"Mary Ann Wilson"));
  EXPECT_EQ(u"C", InitialsFor(u"Cher"));
}

TEST(AvoraLocalUserTest, InitialsAreCapitalised) {
  EXPECT_EQ(u"BG", InitialsFor(u"brannon glover"));
  EXPECT_EQ(u"ÉZ", InitialsFor(u"émile zola"));
}

TEST(AvoraLocalUserTest, InitialsIgnoreSurroundingWhitespaceAndPunctuation) {
  EXPECT_EQ(u"BG", InitialsFor(u"  Brannon   Glover  "));
  EXPECT_EQ(u"OD", InitialsFor(u"(O'Brien) Doyle"));
}

TEST(AvoraLocalUserTest, EmailStandsForItsLocalPart) {
  EXPECT_EQ(u"BG", InitialsFor(u"brannon.glover@example.com"));
  EXPECT_EQ(u"BG", InitialsFor(u"brannon_glover+avora@example.com"));
  EXPECT_EQ(u"B", InitialsFor(u"brannon@example.com"));
}

TEST(AvoraLocalUserTest, NothingToTakeGivesNothing) {
  EXPECT_EQ(u"", InitialsFor(u""));
  EXPECT_EQ(u"", InitialsFor(u"   "));
  EXPECT_EQ(u"", InitialsFor(u"@example.com"));
}

TEST(AvoraLocalUserTest, KeepsCharactersOutsideTheBmpWhole) {
  // U+1D400 MATHEMATICAL BOLD CAPITAL A, a surrogate pair in UTF-16.
  EXPECT_EQ(u"\U0001D400", InitialsFor(u"\U0001D400bc"));
}

}  // namespace
}  // namespace avora
