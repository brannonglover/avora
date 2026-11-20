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
  section + section { margin-top: 16px; }
  input { background: var(--bg); border: 1px solid var(--line); border-radius: 4px;
          color: var(--fg); font: inherit; padding: 5px 8px; }
  input:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  #account-email { min-width: 240px; }
  #account-code { letter-spacing: 0.15em; width: 120px; }
  button:disabled { cursor: default; opacity: 0.5; }
  #style-ai-message:empty, #account-message:empty { display: none; }
  #style-ai-message.error, #account-message.error { color: var(--warn); }
  [hidden] { display: none !important; }
</style>
</head>
<body>
<main>
  <h1>Avora Settings</h1>
  <section id="account" aria-labelledby="account-heading">
    <h2 id="account-heading">Account</h2>
    <p class="muted">Sign in to share one set of tabs with the Avora app on your
      phone and your other Macs: your Spaces, Favorites, pinned pages and
      today's tabs. A tab you open or close on one device opens or closes on
      the others. Avora sends each page's address, title and a small preview
      (never of private windows, or of pages with a password field). Signing
      out removes your Spaces and tabs from this Mac. They stay in your
      account, and signing in again brings them back with the sites you were
      signed in to in each Space.</p>
    <p id="account-state" role="status" aria-live="polite">Loading…</p>
    <p id="account-message" role="alert"></p>
    <form class="row" id="account-email-form" hidden>
      <label for="account-email">Email</label>
      <input id="account-email" type="email" autocomplete="email" spellcheck="false">
      <button id="account-send" class="primary" type="submit">Send code</button>
    </form>
    <form class="row" id="account-code-form" hidden>
      <label for="account-code">Code</label>
      <input id="account-code" inputmode="numeric" autocomplete="one-time-code"
             maxlength="12" spellcheck="false">
      <button id="account-verify" class="primary" type="submit">Sign in</button>
      <button id="account-resend" type="button">Send a new code</button>
      <button id="account-change-email" type="button">Use a different email</button>
    </form>
    <div class="row" id="account-signed-in-row" hidden>
      <label><input id="account-previews" type="checkbox"> Share page previews</label>
      <button id="account-sign-out" type="button">Sign out</button>
    </div>
    <div id="account-sign-out-confirm-row" hidden>
      <p>Sign out? Your Spaces and tabs will be removed from this Mac, and
        its windows and tabs will close, leaving a new tab. They stay in your
        account.</p>
      <div class="row">
        <button id="account-sign-out-confirm" class="primary" type="button">Sign out</button>
        <button id="account-sign-out-cancel" type="button">Cancel</button>
      </div>
    </div>
    <div class="row" id="account-sign-out-anyway-row" hidden>
      <button id="account-sign-out-anyway" type="button">Sign out anyway</button>
    </div>
  </section>
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
  <section id="usage" aria-labelledby="usage-heading">
    <h2 id="usage-heading">Usage statistics</h2>
    <p class="muted">Once a day, Avora tells its developer that this copy was
      used, so we know how many people use it and which versions they're on.
      It sends a random ID for this copy, the Avora and macOS versions, your
      Mac's chip and language, and rough counts of windows, tabs and Spaces.
      Never the pages you visit, their titles, or anything you type. Turning
      this off also forgets the ID.</p>
    <div class="row">
      <label><input id="usage-enabled" type="checkbox"> Send usage statistics</label>
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
(() => {
  const $ = id => document.getElementById(id);
  let lastStatus = null;
  function messageFor(status) {
    switch (status.result) {
      case 'code-sent':
        return [`We emailed a code to ${status.email}. It can take a minute to arrive.`, false];
      case 'signed-in':
        return ['Signed in. Your tabs will be the same on every device in a few seconds.', false];
      case 'signed-out':
        return ['Signed out. Your Spaces and tabs were removed from this Mac and are still in ' +
                'your account.', false];
      case 'invalid-email':
        return ['Enter a valid email address.', true];
      case 'invalid-code':
        return ['Enter the code from the email: 6 or more digits.', true];
      case 'wrong-code':
        return ['That code is wrong or has expired. Check the email, or send a new code.', true];
      case 'rate-limited':
        return ['Too many attempts. Wait a few minutes, then try again.', true];
      case 'network-error':
        return ['Avora could not reach the server. Check your connection and try again.', true];
      case 'session-expired':
        return ['Your session ended, so you were signed out. Sign in again to keep syncing tabs.', true];
      case 'signed-out-device-not-removed':
        return ['Signed out, but the server could not be reached to remove this Mac ' +
                'from your account\'s devices.', true];
      case 'unsynced-changes': {
        const what = status.unsyncedCount === 1
            ? 'A change on this Mac hasn\'t'
            : `${status.unsyncedCount} changes on this Mac haven\'t`;
        return [`${what} reached your account yet, so you are still signed in. ` +
                'Signing out now would lose ' + (status.unsyncedCount === 1 ? 'it' : 'them') +
                '. Check your connection and try again.', true];
      }
      default:
        return [null, false];
    }
  }
  function syncLine(status) {
    if (status.uploadFailing) {
      return 'Tabs could not be synced just now. Avora will keep trying.';
    }
    if (!status.lastUpload) {
      return 'Syncing tabs…';
    }
    const count = status.tabCount === 1 ? '1 tab' : `${status.tabCount} tabs`;
    const when = new Date(status.lastUpload).toLocaleTimeString();
    return `${count} in your account, synced at ${when}.`;
  }
  function stateText(status) {
    if (!status.available) {
      return 'Accounts are not available in private windows.';
    }
    if (status.starting) {
      return 'Loading your account…';
    }
    switch (status.state) {
      case 'signed-in': {
        const memory = status.memoryOnly
            ? ' Secure storage is unavailable, so you will need to sign in again after quitting Avora.'
            : '';
        return `Signed in as ${status.email}. ${syncLine(status)}${memory}`;
      }
      case 'awaiting-code':
        return `Enter the code sent to ${status.email}.`;
      default:
        return 'Not signed in.';
    }
  }
  function render(status) {
    const previousState = lastStatus && lastStatus.state;
    lastStatus = status;
    $('account-state').textContent = stateText(status);
    const [message, isError] = messageFor(status);
    $('account-message').textContent = message || '';
    $('account-message').classList.toggle('error', Boolean(isError));
    const ready = status.available && !status.starting;
    $('account-email-form').hidden = !ready || status.state !== 'signed-out';
    $('account-code-form').hidden = !ready || status.state !== 'awaiting-code';
    const signedIn = ready && status.state === 'signed-in';
    $('account-signed-in-row').hidden = !signedIn;
    if (!signedIn || status.busy) {
      $('account-sign-out-confirm-row').hidden = true;
    }
    $('account-sign-out-anyway-row').hidden =
        !signedIn || status.result !== 'unsynced-changes';
    $('account-previews').checked = Boolean(status.previewsEnabled);
    for (const id of ['account-send', 'account-verify', 'account-resend',
                      'account-change-email', 'account-sign-out',
                      'account-sign-out-confirm', 'account-sign-out-anyway']) {
      $(id).disabled = Boolean(status.busy);
    }
    if (status.state !== previousState) {
      if (status.state === 'awaiting-code') {
        $('account-code').value = '';
        $('account-code').focus();
      } else if (status.state === 'signed-out' && status.email) {
        $('account-email').value = status.email;
      }
    }
  }
  window.avoraSettings.onSyncStatus = render;
  $('account-email-form').addEventListener('submit', event => {
    event.preventDefault();
    chrome.send('sendSyncCode', [$('account-email').value]);
  });
  $('account-code-form').addEventListener('submit', event => {
    event.preventDefault();
    chrome.send('verifySyncCode', [$('account-code').value]);
  });
  $('account-resend').addEventListener('click', () => {
    if (lastStatus) {
      chrome.send('sendSyncCode', [lastStatus.email]);
    }
  });
  $('account-change-email').addEventListener('click', () => chrome.send('cancelSyncSignIn'));
  $('account-sign-out').addEventListener('click', () => {
    $('account-sign-out-confirm-row').hidden = false;
  });
  $('account-sign-out-cancel').addEventListener('click', () => {
    $('account-sign-out-confirm-row').hidden = true;
  });
  $('account-sign-out-confirm').addEventListener('click', () => {
    $('account-sign-out-confirm-row').hidden = true;
    chrome.send('signOutSync', [false]);
  });
  $('account-sign-out-anyway').addEventListener('click', () => chrome.send('signOutSync', [true]));
  $('account-previews').addEventListener(
      'change', event => chrome.send('setSyncPreviews', [event.target.checked]));
  chrome.send('getSyncStatus');
})();
(() => {
  const box = document.getElementById('usage-enabled');
  window.avoraSettings.onTelemetryStatus = status => {
    box.checked = Boolean(status.enabled);
  };
  box.addEventListener('change', event => chrome.send('setTelemetryEnabled', [event.target.checked]));
  chrome.send('getTelemetryStatus');
})();
)JS";

}  // namespace avora_settings

#endif  // CHROME_BROWSER_UI_WEBUI_AVORA_SETTINGS_AVORA_SETTINGS_PAGE_H_
