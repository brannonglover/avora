// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SETTINGS_PAGE_H_
#define CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SETTINGS_PAGE_H_

// chrome://avora-settings is served from these compiled-in strings. The page
// builds its DOM with createElement/textContent only, so it runs under WebUI's
// default Trusted Types CSP.
namespace avora_settings {

inline constexpr char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Avora Settings</title>
<style>
  :root { color-scheme: light dark; --fg: #1f1f1f; --muted: #5f6368; --bg: #fff;
          --card: #f8f9fa; --line: #dadce0; --accent: #1a73e8; --warn: #b3261e; }
  @media (prefers-color-scheme: dark) {
    :root { --fg: #e8eaed; --muted: #9aa0a6; --bg: #202124; --card: #292a2d;
            --line: #3c4043; --accent: #8ab4f8; --warn: #f2b8b5; }
  }
  body { background: var(--bg); color: var(--fg); font: 14px/1.5 system-ui, sans-serif;
         margin: 0; padding: 32px 16px; }
  main { margin: 0 auto; max-width: 680px; }
  h1 { font-size: 22px; font-weight: 500; margin: 0 0 24px; }
  section { background: var(--card); border: 1px solid var(--line); border-radius: 8px;
            padding: 16px 20px; }
  h2 { font-size: 16px; font-weight: 500; margin: 0 0 4px; }
  p { margin: 8px 0; }
  .muted { color: var(--muted); }
  .row { align-items: center; display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; }
  button, select { font: inherit; }
  button { background: transparent; border: 1px solid var(--line); border-radius: 4px;
           color: var(--accent); cursor: pointer; padding: 6px 14px; }
  button.primary { background: var(--accent); border-color: var(--accent); color: var(--bg); }
  button:focus-visible, select:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  select { background: var(--bg); border: 1px solid var(--line); border-radius: 4px;
           color: var(--fg); padding: 5px 8px; }
  #style-ai-message:empty { display: none; }
  #style-ai-message.error { color: var(--warn); }
  [hidden] { display: none !important; }
</style>
</head>
<body>
<main>
  <h1>Avora Settings</h1>
  <section id="style-ai" aria-labelledby="style-ai-heading">
    <h2 id="style-ai-heading">Style Studio AI</h2>
    <p class="muted">Style Studio in DevTools can generate CSS with Anthropic's
      models. Generation uses your own Anthropic API key and runs only when you
      press Generate. The key is stored encrypted on this device and is never
      shown again.</p>
    <p id="style-ai-state" role="status" aria-live="polite">Loading…</p>
    <p id="style-ai-message" role="alert"></p>
    <div class="row" id="style-ai-key-row">
      <button id="style-ai-add" class="primary" type="button">Add API key…</button>
      <button id="style-ai-remove" type="button" hidden>Remove key</button>
    </div>
    <div class="row" id="style-ai-model-row">
      <label for="style-ai-model">Model</label>
      <select id="style-ai-model"></select>
    </div>
  </section>
</main>
<script src="avora_settings.js"></script>
</body>
</html>
)HTML";

inline constexpr char kSettingsJs[] = R"JS('use strict';
(() => {
  const $ = id => document.getElementById(id);
  const MESSAGES = {
    'saved': ['The key was saved.', false],
    'removed': ['The key was removed.', false],
    'model-saved': ['The model was saved.', false],
    'model-rejected': ['That model is not available.', true],
    'invalid-format': ['That does not look like an API key, so it was not saved.', true],
    'storage-not-ready': ['Secure storage is still starting. Try again in a moment.', true],
    'storage-unavailable':
        'Secure storage is unavailable (for example, Keychain access was denied), so the key was not saved.',
    'insecure-storage-refused':
        'This browser is running with a test keychain that cannot protect a key, so the key was not saved.',
  };
  function messageFor(result) {
    const entry = MESSAGES[result];
    if (!entry) {
      return [null, false];
    }
    return Array.isArray(entry) ? entry : [entry, true];
  }
  function stateText(status) {
    if (!status.available) {
      return 'Style Studio AI is not available in private windows.';
    }
    switch (status.keyState) {
      case 'present': {
        const saved = status.savedAt ? new Date(status.savedAt).toLocaleString() : null;
        const test = status.insecureTestKey ? ' (test key, usable only with the local test provider)' : '';
        return saved ? `An API key is saved${test}. Saved ${saved}.` : `An API key is saved${test}.`;
      }
      case 'undecryptable':
        return 'The saved key can no longer be read on this device. Enter it again.';
      case 'storage-not-ready':
        return 'Secure storage is starting…';
      default:
        return 'No API key is saved.';
    }
  }
  function render(status) {
    $('style-ai-state').textContent = stateText(status);
    const [message, isError] = messageFor(status.lastResult);
    $('style-ai-message').textContent = message || '';
    $('style-ai-message').classList.toggle('error', Boolean(isError));
    const hasKey = status.keyState === 'present' || status.keyState === 'undecryptable';
    $('style-ai-add').textContent = hasKey ? 'Replace key…' : 'Add API key…';
    $('style-ai-add').disabled = !status.available;
    $('style-ai-remove').hidden = !hasKey || !status.available;
    $('style-ai-model-row').hidden = !status.available;
    const select = $('style-ai-model');
    select.replaceChildren(...status.models.map(model => {
      const option = document.createElement('option');
      option.value = model;
      option.textContent = model;
      option.selected = model === status.model;
      return option;
    }));
  }
  window.avoraSettings = {onStyleAiStatus: render};
  $('style-ai-add').addEventListener('click', () => chrome.send('openStyleAiKeyDialog'));
  $('style-ai-remove').addEventListener('click', () => chrome.send('removeStyleAiKey'));
  $('style-ai-model').addEventListener('change', event => chrome.send('setStyleAiModel', [event.target.value]));
  chrome.send('getStyleAiStatus');
})();
)JS";

}  // namespace avora_settings

#endif  // CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SETTINGS_PAGE_H_
