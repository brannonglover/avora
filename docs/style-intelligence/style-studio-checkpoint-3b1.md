# Style Studio — Checkpoint 3B.1: secure credential storage and Settings

Date: 2026-10-21. Base: `35bfb61`. Plan: `style-studio-checkpoint-3a-ai-architecture.md` §9.2 and §10.

**Scope:** storing and managing the Anthropic API key and model choice. Nothing in this checkpoint sends a request, reads a key for a request, or touches DevTools.

## What the user sees

**chrome://avora-settings** (Avora Settings, opened from the Avora toolbar) now has a **Style Studio AI** section:
- **What it explains:** what the key is for; that it's used only when Generate is pressed; that it's stored encrypted and never shown again.
- **Status line** (`role="status"`):
  - "No API key is saved."
  - "An API key is saved. Saved *date*."
  - "The saved key can no longer be read on this device. Enter it again."
  - "Secure storage is starting…"
  - In private windows: "Style Studio AI is not available in private windows."
- **Add API key… / Replace key…** opens a **native dialog** with a password field. **Remove key** deletes the key.
- **Model** offers `claude-opus-5-5` (default), `claude-sonnet-5-5` and `claude-haiku-5-5`.
- **Result messages** (`role="alert"`):
  - saved;
  - removed;
  - model saved or rejected;
  - doesn't look like an API key;
  - secure storage still starting;
  - secure storage unavailable (for example, Keychain access denied), so not saved;
  - test keychain can't protect a key, so not saved.

## How the key is handled

| Step | Where | Guarantee |
| --- | --- | --- |
| Entry | A native `views::Textfield` with `TEXT_INPUT_TYPE_PASSWORD` in the browser process (`avora_style_ai_key_dialog.cc`) | Never in a renderer. The Settings page only asks the browser to open the dialog. The field is cleared on Save. |
| Format check | `StyleAiKeyStore::IsWellFormedKey` | Printable ASCII, no whitespace, 20–256 characters. Anything else is refused and nothing is stored. |
| Encryption | `os_crypt_async::Encryptor::EncryptString` → Base64 → `avora.style_ai.credential` `{ciphertext, saved_at[, insecure_test]}` | One pref write, so a replacement is atomic. If encryption is not ready or not available, nothing is stored and an existing key is left as it was. |
| On macOS | The encryption key comes from the "Avora Safe Storage" Keychain item (PBKDF2, AES-128-CBC) | Protects against disk inspection, not against code running as the user. The same limit applies to saved passwords and Live Folders. |
| Keychain unavailable | `IsEncryptionAvailable()` is false, or `EncryptString` fails | `storage-unavailable`: not saved, with a message. |
| `--use-mock-keychain` | `StyleAiSettings::EnvironmentFor` | Real keys are refused (`insecure-storage-refused`). Only a non-official build with `--avora-style-ai-testing` accepts one, marked `insecure_test`. Such a key is unusable outside that environment, and 3B.2 will allow it only against the local fake provider. |
| Status to the page | `StyleAiStatusForSettings` | Exactly `available, reason, keyState, insecureTestKey, savedAt, environment, model, models, lastResult`. Never the key, a fragment of it, or the ciphertext. A test pins this field set. |
| Undecryptable | The profile moved machines, or the Keychain item was reset | Reported as "enter it again". Avora adds no fallback. |
| Remove | `ClearPref` | The pref is empty afterwards. |
| Off the record | `StyleAiSettings::GetForProfile` returns null | Unavailable. The dialog can't be opened and nothing is created. |
| Sync | Prefs registered without sync flags | Neither the ciphertext nor the model leaves the device. A test pins this. |

`GetKeyForRequest()` is the only decrypting accessor. Nothing calls it yet; the 3B.2 service will use it to build one request.

## Files

- **New (Avora-owned):**
  - `src/chrome/browser/avora/style_ai/avora_style_ai_key_store.{h,cc}`
  - `src/chrome/browser/avora/style_ai/avora_style_ai_settings.{h,cc}`
  - `src/chrome/browser/avora/style_ai/avora_style_ai_status.{h,cc}`
  - `src/chrome/browser/avora/style_ai/avora_style_ai_key_store_unittest.cc`
  - `src/chrome/browser/avora/style_ai/avora_style_ai_status_unittest.cc`
  - `src/chrome/browser/ui/views/avora/avora_style_ai_key_dialog.{h,cc}`
  - `src/chrome/browser/ui/views/avora/avora_style_ai_settings_browsertest.cc`
  - `src/chrome/browser/ui/webui/avora_settings/avora_settings_page.h`
  - `src/chrome/browser/ui/webui/avora_settings/avora_style_ai_handler.{h,cc}`
- **Modified (Avora-owned):**
  - `src/chrome/browser/avora/BUILD.gn`: sources; a `style_ai_unit_tests` source set and an `avora_style_ai_unit_tests` binary, also linked into `avora_unit_tests`;
  - `src/chrome/browser/avora/avora_prefs.cc`: pref registration;
  - `src/chrome/browser/ui/views/avora/BUILD.gn`;
  - `src/chrome/browser/ui/webui/avora_settings/{avora_settings_ui.cc,BUILD.gn}`: the page, served from compiled-in strings, and the handler.
- **Modified (existing overlay):** `src/chrome/browser/ui/views/frame/browser_view.cc`. It hands `StyleAiSettings` the OSCryptAsync instance and the dialog launcher, next to the existing Live Folders line.

**No new Chromium overlays.** On a roll, only `browser_view.cc`, which was already overlaid, gains three lines.

## Verification

Fresh build: `autoninja -C out/Default chrome avora_style_ai_unit_tests avora_browser_tests`.

| Suite | Result |
| --- | --- |
| `out/Default/avora_style_ai_unit_tests` | 20/20 |
| `out/Default/avora_browser_tests --gtest_filter='AvoraStyleAi*'` | 7/7 |
| `out/Default/avora_browser_tests`, full Avora suite (mini window, link preview, Style AI) | 48/48 |
| Live: chrome://avora-settings in the built Avora | renders; no console or CSP errors; `Preferences` holds no Style AI data until a key is saved |

**Unit tests** (key store and status):
- an encrypted round trip;
- the pref holds no plaintext or fragment;
- the prefs aren't syncable;
- malformed keys are refused;
- refused before the encryptor arrives, and reported "not ready";
- refused when secure storage is unavailable, either with no key or with encryption reported unavailable, leaving an existing key unchanged;
- undecryptable or corrupt ciphertext reads as needing re-entry;
- replace and remove;
- mock-keychain refusal;
- insecure test keys usable only in their environment;
- the model allowlist and default;
- environment detection from the switches;
- the status never carries the key or ciphertext, and has exactly the documented fields;
- off-the-record status;
- every save result has a name.

**Browser tests** use the real page, the real handler, and the **real native dialog** shown as a widget, with text typed into its password field and Save pressed:
- the key is saved encrypted and the page never receives it (DOM and `window` checked);
- replace, then remove;
- a malformed key is refused;
- Cancel stores nothing;
- only allowed models are stored, and a page request for any other is refused;
- a real key is refused under a mock keychain without the testing switch;
- private windows show the feature as unavailable, and a forced request opens no dialog.

## Not covered, and found along the way

**Not covered:**
- Saving with the real macOS Keychain was not exercised automatically: a rebuilt, ad-hoc-signed app triggers a Keychain access prompt. The path is identical to Live Folders' through `Encryptor`, and the unavailable branch is unit-tested.
- Provider verification of the key (`GET /v1/models/{id}`) is 3B.2.

**Pre-existing breakage, outside this checkpoint and unchanged:** `avora_unit_tests` doesn't compile at HEAD.
- `chrome/browser/avora/import/html_bookmark_parser_unittest.cc` and `chrome_bookmark_parser_unittest.cc` (from `36d8de3`) end raw strings early (`void(0)"`, `(1)"`), are missing includes, and have an unused function.
- So the Style AI tests also build as `avora_style_ai_unit_tests`.
- Fixing the import tests is a separate task.
