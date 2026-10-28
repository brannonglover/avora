// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_AVORA_LOCAL_USER_H_
#define CHROME_BROWSER_AVORA_AVORA_LOCAL_USER_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avora {

// The person using this computer, as the operating system knows them.
//
// Avora has no account picture of its own -- an Avora account is an email
// address and nothing more -- so the avatar in the sidebar is drawn from the
// OS account instead: on macOS, the picture and full name in System
// Settings > Users & Groups.  Nothing here leaves the machine.
struct LocalUser {
  // "Brannon Glover".  Empty when the OS has no name to give.
  std::u16string full_name;

  // The account picture as PNG, or empty when there is none.
  std::vector<uint8_t> photo_png;
};

// Reads the OS account's name and picture.  Blocking -- the lookup goes
// through the system's directory services -- so call it off the UI thread.
LocalUser GetLocalUserBlocking();

// Up to two capital letters standing for |name|: the first letters of its
// first and last words ("Brannon Glover" -> "BG", "Cher" -> "C").  An email
// address stands for its local part, split on '.', '_' and '-'
// ("brannon.glover@example.com" -> "BG").  Empty when |name| has no letters
// or digits to take.
std::u16string InitialsFor(std::u16string_view name);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_AVORA_LOCAL_USER_H_
