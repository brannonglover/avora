// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_local_user.h"

#include <algorithm>

#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/third_party/icu/icu_utf.h"
#include "build/build_config.h"
#include "third_party/icu/source/common/unicode/uchar.h"

namespace avora {

namespace {

bool IsSeparator(char16_t c, bool is_email) {
  return base::IsUnicodeWhitespace(c) ||
         (is_email && (c == u'.' || c == u'_' || c == u'-'));
}

// The first letter or digit of |word|, as a whole code point so a name that
// starts outside the BMP is not cut in half.
std::u16string FirstLetter(std::u16string_view word) {
  for (size_t i = 0; i < word.size(); ++i) {
    const size_t start = i;
    base_icu::UChar32 code_point;
    // Leaves |i| on the code point's last code unit.
    if (base::ReadUnicodeCharacter(word, &i, &code_point) &&
        u_isalnum(code_point)) {
      return std::u16string(word.substr(start, i - start + 1));
    }
  }
  return std::u16string();
}

}  // namespace

std::u16string InitialsFor(std::u16string_view name) {
  const bool is_email = name.find(u'@') != std::u16string_view::npos;
  if (is_email) {
    // "+avora" in "brannon+avora@" is a mailbox tag, not part of the name.
    name = name.substr(0, std::min(name.find(u'@'), name.find(u'+')));
  }

  std::vector<std::u16string> letters;
  size_t start = 0;
  for (size_t i = 0; i <= name.size(); ++i) {
    if (i == name.size() || IsSeparator(name[i], is_email)) {
      std::u16string letter = FirstLetter(name.substr(start, i - start));
      if (!letter.empty()) {
        letters.push_back(std::move(letter));
      }
      start = i + 1;
    }
  }
  if (letters.empty()) {
    return std::u16string();
  }

  std::u16string initials = letters.front();
  if (letters.size() > 1) {
    initials += letters.back();
  }
  return base::i18n::ToUpper(initials);
}

#if !BUILDFLAG(IS_MAC)
LocalUser GetLocalUserBlocking() {
  return LocalUser();
}
#endif

}  // namespace avora
