// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_STATUS_H_
#define CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_STATUS_H_

#include <optional>
#include <string>

#include "base/values.h"
#include "chrome/browser/avora/style_ai/avora_style_ai_key_store.h"

namespace avora {

// The machine-readable name of a save result, as Settings shows it.
std::string SaveResultName(StyleAiKeyStore::SaveResult result);

// What Avora Settings may know about Style Studio AI: whether it is available,
// whether a key is stored and usable, when it was saved, and the model. Never
// the key or any part of it. `store` is null for off-the-record profiles.
//
//   {available: bool, reason: "off-the-record"|null,
//    keyState: "absent"|"present"|"undecryptable"|"storage-not-ready",
//    insecureTestKey: bool, savedAt: double (ms)|null,
//    environment: "secure"|"mock-keychain"|"insecure-test",
//    model: string, models: [string], lastResult: string|null}
base::DictValue StyleAiStatusForSettings(
    const StyleAiKeyStore* store,
    const std::optional<std::string>& last_result);

}  // namespace avora

#endif  // CHROME_BROWSER_AVORA_STYLE_AI_AVORA_STYLE_AI_STATUS_H_
