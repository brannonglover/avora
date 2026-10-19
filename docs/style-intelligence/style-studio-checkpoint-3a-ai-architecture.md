# Style Studio — Checkpoint 3A: AI integration architecture

Date: 2026-10-19. Base: `f7b9751` (Checkpoint 2B). Research and design only: no production code changed.

**Status: proposal, not approved.** Everything marked *Recommendation* or *recommended* is a proposal. The decisions that need approval before Checkpoint 3B starts are listed in §9; until they're made, nothing here is decided.

**Summary.**
- Upstream DevTools AI (AIDA) cannot run in Avora. Its transport, streaming, cancellation and validation patterns are reusable, but nothing on its provider path is.
- **Recommendation:** a per-profile **Avora browser-process service** that holds a **user-supplied API key** encrypted with OSCryptAsync and makes the provider request itself. DevTools reaches it through three new embedder messages.
- The DevTools frontend never sees the key and never talks to the provider.
- The model sees a small, allowlisted, size-capped description of one element. It returns CSS declarations only.
- Those declarations go through the same parser as manual drafts, plus stricter rules, and land in the **draft editor**. Applying them stays a separate, explicit user action through the existing preview engine.

---

## 1. Existing infrastructure

### 1.1 Upstream DevTools frontend (`third_party/devtools-frontend/src/front_end`)

| Area | What exists | Reusable in Avora? |
| --- | --- | --- |
| Agents | `models/ai_assistance/agents/AiAgent.ts` (`AiAgent` :510, request builder :642-689, 10-step loop :185, approval-gated side effects :1032-1064). `StylingAgent.ts` (preamble :28-64; tools `GET_STYLES`, `EXECUTE_JAVASCRIPT`) | **No.** Built for multi-step agentic chat with function calling and page JavaScript, which is exactly what Style Studio must not allow (§4). |
| Element context | `contexts/DOMNodeContext.ts:108-195` describes a node by selectors and backend ids, never raw `outerHTML`. `tools/GetStyles.ts` returns computed and authored styles, origin-locked. | **As a pattern.** Studio builds its own allowlisted context (§3). |
| Applying CSS | `ExtensionScope.ts` + `injected.ts`: adds an `ai-style-change-N` **class to the page element** (DOM mutation), then `ChangeManager.ts` writes rules into an inspector stylesheet. | **No.** It mutates the DOM and bypasses Studio's preview engine and ownership ledger. |
| CSS validation | `ExtensionScope.sanitizedStyleChanges` (:332-369) parses values in a constructed `CSSStyleSheet` | Same idea as Studio's `parseDeclarations`, which already exists. |
| Client | `core/host/AidaClient.ts` (`doConversation` :155-234, chunk parser :308-331, typed errors :64-88). `GcaClient.ts` and `AidaGcaTranslation.ts` handle the Gemini API shape. | **No.** It's AIDA-specific. Agents take the concrete `AidaClient` class (`AiAgent.ts:167`); there's no provider interface. |
| Transport | `InspectorFrontendHost.doAidaConversation`, `dispatchHttpRequest` (`InspectorFrontendHostAPI.ts:442-445`). The frontend sends `{service, path, …}`, never a URL. Streaming uses `bindOutputStream` / `streamWrite` (`ResourceLoader.ts:77-89`). | **The pattern, yes.** A browser-owned endpoint plus a frontend that names a service. The streaming plumbing is generic. |
| Credentials | The frontend never handles tokens or keys. | The same rule applies to Avora. |
| Gating | `hostConfig.aidaAvailability` (`core/root/Runtime.ts:334-341`), `ai-assistance-enabled` (`AiUtils.ts:82-113`), enterprise `GenAiEnterprisePolicyValue` | **No.** Every check reports "unavailable" in Avora (§1.2). |
| Cancellation | `AbortController` per run (`AiAssistancePanel.ts:620, 1603`). Abort only rejects on the frontend side: **no cancel reaches the browser** (`DispatchHttpRequestClient.ts:22-39`). | Avora must add a real browser-side abort. |
| Timeouts | No frontend LLM timeout. The browser caps at 120 s. | Avora sets its own (§7). |
| History | `AiHistoryStorage` keeps conversations in Settings, with tool outputs redacted. | Not needed: 3A has no conversation history. |
| On-device | `BuiltInAi.ts` (Prompt API, `window.LanguageModel`), behind a console-teaser flag | Possible future offline option (§2). Not first. |

**Prompt-injection handling upstream** is structural rather than textual:
- origin locking (`AiOrigins.ts`);
- header and data redaction (`NetworkRequestFormatter.ts:19-81`);
- an isolated world with `connect-src 'none'`;
- approval for side effects.

There are no "untrusted data" delimiters in the prompts.

### 1.2 Upstream browser process (`chrome/browser/devtools`)

- **Endpoints and credentials are hard-coded and Google-only.** `aida.googleapis.com` (`aida_client.h:43-48`). OAuth access token for the signed-in Google account, scope `…/auth/aida` (`aida_client.cc:155-205`). Sent with `CredentialsMode::kOmit` and `LOAD_DISABLE_CACHE`.
- **The availability gate is compiled out.** It requires `BUILDFLAG(GOOGLE_CHROME_BRANDING)` (`aida_client.cc:114`).
- **Request handling:**
  - Requests are streamed with a hard 120 s timeout (`devtools_ui_bindings.cc:1064`).
  - 5xx and 429 responses are retried with backoff; a 401 refreshes the token (:1106-1136).
  - `dispatchHttpRequest` only serves local `devtools://` frontends (`is_local_frontend_`, :1168) and allowlisted services (`devtools_http_service_registry.cc:62-88`). Off-the-record profiles are refused.
- **No browser-side abort.** A loader runs until it completes.

**Why AIDA can't work in Avora:**
- **Not branded.** Avora is not Chrome-branded, so the gate always reports "blocked".
- **No credentials.** `build-scripts/args.gn:21` sets `google_api_key = "no"` and leaves the OAuth client unset, so tokens can't be minted either.
- **Not assumed available.** Google AI services are not assumed available in Avora, and none of that path is reused.

### 1.3 Avora today

- **No AI, LLM or provider code anywhere in the overlay.**
- **One existing secret-storage pattern** to copy: `src/chrome/browser/avora/avora_live_folder_credentials.{h,cc}` (`LiveFolderCredentialStore`).
  - It stores a GitHub token in a profile pref as `os_crypt_async::Encryptor::EncryptString` ciphertext, Base64-encoded.
  - It refuses to store when encryption isn't ready or fails.
  - The Keychain item is "Avora Safe Storage" (`components/os_crypt/common/keychain_password_mac.mm:37-43`).
  - It has a traffic annotation, validates the token before saving, and is not created for off-the-record profiles.
- **Per-profile services** follow `LiveFolderService` (`SupportsUserData`, `avora_live_folder_service.cc`).
- **Avora has never overlaid `chrome/browser/devtools/`.** Adding embedder messages would be the first such overlay.
- **Embedder plumbing** for a new DevTools method:
  - **Browser side:** a `Delegate` virtual in `devtools_embedder_message_dispatcher.h`, `RegisterHandlerWithCallback` in `CreateForDevToolsFrontend` (`.cc:343-454`), and the implementation in `DevToolsUIBindings`.
  - **Frontend side:** a declaration in `InspectorFrontendHostAPI.ts`, the call in `devtools_compatibility.js` (`sendMessageToEmbedder`), and a stub in `InspectorFrontendHostStub.ts`.
  - **Remote-debugging frontends** (`http://localhost:port/devtools`) get the stub and no bindings.

## 2. Provider options

| | 1. Frontend calls the provider directly | 2. Avora browser-process service | 3. Avora-controlled backend | 4. User-supplied key, stored by Avora |
| --- | --- | --- | --- | --- |
| Where the secret lives | In the DevTools renderer: readable by any frontend code or extension-like injection, and logged in DevTools' own network panel | Browser process only | Avora's server | (Combined with 2) Keychain-wrapped OSCrypt ciphertext in a profile pref |
| Security | **Poor.** The key sits in a renderer; it needs CORS or `connect-src` exceptions in the DevTools frontend CSP; a remote frontend could be asked for it. | **Good.** The renderer can only ask the service to run a validated request. | Good for keys. The server becomes a high-value target holding every user's traffic. | Good: the same OSCrypt boundary as Live Folders. |
| Privacy | Page data goes to the provider. | Page data goes to the provider; Avora sees nothing. | Page data goes to Avora **and** the provider; Avora must run a retention and logging policy. | Same as 2. The user's own provider account and terms apply. |
| Operating cost to Avora | none | none | inference plus infrastructure, abuse prevention, accounts | none (the user pays the provider) |
| Implementation complexity | low, but unsafe | **medium.** A C++ service, three embedder messages, a settings UI. | high: a service, auth, billing, quotas, ops. | small on top of 2 |
| Offline | fails | fails, with a clear "offline" state | fails | fails |
| Fit with Avora's Chromium tree | DevTools CSP changes; no browser code | follows `LiveFolderService` + `DevToolsUIBindings` patterns; one new overlay of `chrome/browser/devtools` | needs 2 anyway for the transport | uses existing OSCryptAsync wiring |
| Remote-debugging frontend | would work, with the same key leak | unavailable (no bindings), stated in the UI | unavailable | unavailable |

**Recommendation: options 2 and 4 together.** An `AvoraStyleAiService` in the browser process, with the user's own provider key stored with OSCryptAsync.

Reasons:
- The key never enters a renderer.
- Avora runs no servers, stores no page data and pays no inference cost.
- Users choose and see their own provider terms.
- The pieces already exist in Avora (OSCryptAsync wiring, the credential-store pattern, per-profile services, traffic annotations).
- An Avora backend (3) can later sit behind the same service interface without changing the frontend.

**Rejected for the first implementation:**
- **1**, because the secret would sit in a renderer.
- **3**, because of cost, ops and privacy liability with no first-release benefit.
- **On-device models**, because they aren't available on every Avora build and quality is unproven. They're worth revisiting as an offline mode.

## 3. Model context

**Principle:** describe *one element's styling situation*, never the page. Everything comes from data Style Intelligence and Studio already compute. Page-originated strings are treated as untrusted data (§6).

**Request schema** (frontend → browser, JSON, `version: 1`):

```ts
interface StyleGenerationRequestV1 {
  version: 1;
  /** The user's natural-language request. 1–1,000 characters. */
  instruction: string;
  target: {
    tag: string;                 // localName, ≤ 64
    id: string|null;             // ≤ 64
    classes: string[];           // ≤ 16 items, each ≤ 64
    role: string|null;           // explicit `role` attribute token only, ≤ 32
    scope: 'node'|'all-matches';
    authoringSelector: string;   // the selector the CSS is for, ≤ 256
    matchCount: number|null;     // for all-matches
  };
  geometry: {width: number, height: number}|null;   // CSS px, rounded
  /** Allowlisted computed properties for the target (§3.1), values ≤ 200 characters. */
  computed: Record<string, string>;
  /** Matched rules that set allowlisted properties, highest priority first; ≤ 12 rules. */
  matchedRules: Array<{
    selector: string;            // ≤ 256
    origin: 'author'|'inline'|'user-agent'|'inspector';
    layered: boolean;
    declarations: Array<{name: string, value: string, important: boolean}>;  // allowlisted names; ≤ 24 per rule
  }>;
  /** Style Intelligence's own layout summary. */
  layout: {
    statements: string[];        // e.g. "Flex row, wraps", ≤ 4, each ≤ 120
    parent: {tag: string, display: string|null, flexDirection: string|null, gap: string|null,
             gridTemplateColumns: string|null}|null;
    ancestors: Array<{tag: string, display: string|null, position: string|null}>;  // ≤ 3
  };
  /** Design tokens in use: custom-property names and resolved values. ≤ 20, each ≤ 120. */
  tokens: Array<{customProperty: string, resolvedValue: string|null}>;
}
```

**Limits:**
- **Size.** The serialised request is ≤ 16 KiB. Every field has its own cap. Lists are truncated to their caps, and a request still over the limit is refused, never sent partially. The browser validates the same schema again and refuses anything else.

### 3.1 Privacy boundaries

**Sent:**
- tag, `id`, class tokens and explicit `role`;
- the authoring selector;
- rounded box size;
- about 60 allowlisted layout and visual computed properties: display, position, box model, flex and grid, typography, colour, background-color, border, radius, shadow, opacity, z-index, overflow, transform, transition;
- matched rules restricted to those properties;
- Style Intelligence's layout statements;
- parent and ancestor tag and display;
- custom-property names and values.

**Never sent:**
- HTML, text content, `innerText`, `value` or any form state;
- attributes other than `id`, `class` and `role` (no `href`, `src`, `alt`, `title`, `data-*`, `aria-*` values);
- the page URL, origin or title;
- cookies, storage or headers;
- screenshots;
- other elements' styles beyond the parent and ancestor summary;
- the user's existing draft.

**Scrubbed:**
- Any value containing `url(` becomes `url(<omitted>)`.
- The `content`, `quotes`, `counter-*` and `font-feature-settings` properties are excluded, since they can carry page text.
- Strings are stripped of control characters.

**Known residual:** class names, ids and custom-property names are authored by the page and can occasionally contain identifiers such as `user-4821`. They are capped, documented in the consent text, and treated as untrusted data.

## 4. Generation contract

The model is asked for **one JSON object, nothing else**:

```json
{"declarations": "color: #1a1a1a; padding: 8px 12px;", "note": "Short plain-text explanation, ≤ 280 chars."}
```

Where the provider supports it, a JSON schema or structured output is requested. No tools or function calling are offered.

**Validation pipeline** (output is untrusted data):
1. **Browser service**, transport level:
   - HTTP status and provider error mapping;
   - response ≤ 32 KiB;
   - extract the single text output;
   - parse JSON;
   - require exactly `declarations` (string ≤ 4 KiB) and an optional `note` (string ≤ 280);
   - anything else is `malformed-response`.

   The service never interprets CSS.
2. **Frontend (`StyleStudioAi` validator)**, CSS level:
   - Run `parseDeclarations` (the same browser-parser path as manual drafts). It rejects `}`, rules, nesting and at-rules, and returns the browser's serialisation plus dropped declarations.
   - **Reject** the whole result (`unsafe-output`) if any declaration:
     - has `!important` — the contract forbids adding it, and the user can type it by hand;
     - uses `url(`, `image-set(`, `image(`, `-webkit-image-set(`, `src(` or `@import` — this blocks network fetches and exfiltration through previews;
     - uses `expression(`, `-moz-binding` or `behavior`;
     - is a property outside a generous visual/layout allowlist (no `content`, no `-webkit-user-modify`).
   - Limits: ≤ 40 declarations; a result with zero valid declarations is `no-css`.
   - Invalid declarations dropped by the parser are listed to the user, as for manual drafts.
3. **Note.** Rendered as plain text, never Markdown or HTML.

**By construction:**
- **Nothing executes.** No JavaScript, HTML or DOM mutation is possible: the output is a string placed in a `<textarea>`.
- **No targeting or ownership decisions.** Selectors and ownership are not part of the output; target and scope stay Studio's.
- **No path around the engine.** Nothing is applied automatically; the generated CSS can reach the page only through **Apply preview**, which runs the 2A engine with all its checks.
- **No page or protocol access.** The model has no tools, and the browser service has no DevTools Protocol access.

## 5. User experience

In the Studio tab's **Preview** section, above the CSS editor:

1. **Choose a target** (existing).
2. **Describe the change** in a labelled field ("Describe the change", ≤ 1,000 characters), then press **Generate CSS** (or Ctrl/Cmd+Enter in that field).
   - Generate is disabled without a valid target, while a generation runs, or when AI isn't set up.
3. **While generating:** the status reads "Generating CSS…" and **Cancel** appears. Apply, Update and Revert keep working on the current preview.
4. **When it arrives:**
   - **Empty draft:** the declarations fill the draft. The status reads "Generated CSS is in the draft. Review it, then apply." The note appears as plain text.
   - **Draft not empty:** nothing is overwritten. An inline confirmation shows the generated declarations: "Replace your draft with the generated CSS?" **Replace** / **Keep my draft**.
5. **The user edits and applies** with the existing controls. Generation never applies anything.

**Interruptions:**
- **Target, scope, selector or document change during generation:** the request is aborted, the result is discarded, and the status reads "The target changed, so generation was cancelled."
- **A result that arrives for a stale target is never shown.** Each request carries a generation id and the target key.

**First use:** a consent panel states exactly what is sent (§3.1), to which provider, under whose account and terms, and that Avora keeps no copy. It offers **Continue** and **Not now**.

**Not configured:** "Set up a provider key in Avora Settings → Style Studio AI" with a link. The remote-debugging frontend shows "AI generation is available in docked DevTools only."

**Accessibility:** native fields, a `role="status"` live region and `devtools-button`s, as in 2B. Conversation history and refinement are out of scope.

## 6. Privacy and security

| Topic | Design |
| --- | --- |
| What leaves the browser | Only the §3 request plus provider protocol framing, sent to the provider the user configured. Nothing goes to Avora. |
| Credential storage | The provider key is in a profile pref as OSCryptAsync ciphertext, Base64-encoded. The encryption key sits in the macOS Keychain ("Avora Safe Storage"), as for Live Folders. It's never written in clear, never sent to a renderer, never synced; if encryption isn't ready, the key isn't stored. |
| Entering the key | In Avora Settings (a settings WebUI handler calls the service). It's write-only: the UI shows "Key saved (…last 4)", **Replace** and **Remove**. |
| Authentication | The service adds the provider's auth header at request time and sends with `CredentialsMode::kOmit` and `LOAD_DISABLE_CACHE`. No cookies. |
| Request authorisation | The embedder messages are honoured only for local `devtools://` frontends (as `dispatchHttpRequest`), only in regular profiles (off-the-record off by default, open decision), and only after consent. The browser re-validates the request schema and caps. |
| Logging | Avora logs no request or response bodies, keeps no history, and sends no telemetry. Error codes may go to `DVLOG`. Provider-side retention follows the user's provider account and is disclosed in the consent text. |
| Network transparency | A traffic annotation (destination OTHER, `cookies_allowed: NO`) with a description and user-facing setting. |
| Prompt injection | Page-originated strings (selectors, class names, custom-property values, rule text) go only inside a JSON data block. The system instructions say that block is data to style, never instructions. The model has no tools, and the output is format-constrained and validated (§4). **Worst case:** unwanted but valid CSS in a draft the user reviews before applying. Network-fetching CSS is rejected, which closes the exfiltration path. |
| Generated CSS | Validated twice (§4), then goes through the 2A preview engine (fail-closed selector, ownership checks, ledger) only on explicit Apply. |
| Provider unavailable | Clear states: not configured, offline, invalid key, rate limited, provider error, timeout. The manual editor and previews are unaffected. Nothing is retried silently except one retry on 5xx or 429 with backoff. |

**Data flow:**

```
 DevTools frontend (renderer)                 Browser process                        Provider
 ───────────────────────────                  ───────────────                        ────────
 Studio target + Style Intelligence data
        │ build context (allowlist, caps, scrub)
        ▼
 StyleGenerationRequestV1 (≤16 KiB) ──avoraStyleGenerate(json, id)──▶ AvoraStyleAiService
                                                                       │ check: local devtools://,
                                                                       │ consented, profile, schema/caps
                                                                       │ decrypt key (OSCryptAsync)
                                                                       ▼
                                                                  provider adapter ──HTTPS──▶ model API
                                                                       │◀── response (≤32 KiB) ──┘
                                                                       │ extract + JSON check
        ┌───────── {declarations, note} | {error} ◀────────────────────┘
        ▼
 validate CSS (parseDeclarations + rules) ─▶ draft editor (confirm before replacing)
        │
        ▼  only on user's "Apply preview"
 StylePreviewEngine (2A/2B) ─▶ Studio's inspector stylesheet ─▶ page

 avoraStyleAbort(id) ──▶ cancels the URL loader          (key never crosses into the renderer)
```

## 7. Errors and cancellation

| Code | Cause | UI |
| --- | --- | --- |
| `not-configured` | no key stored | set-up message with link; Generate disabled |
| `consent-required` | first use | consent panel |
| `unavailable-frontend` | remote-debugging frontend | "docked DevTools only" |
| `offline` / `network` | `net::` error | "Couldn't reach the provider. Check your connection." |
| `invalid-key` | 401/403 | "The provider rejected the key. Replace it in Avora Settings." |
| `rate-limited` | 429 after one retry | "The provider is rate limiting. Try again shortly." |
| `provider-error` | 5xx after one retry, or a provider error body | "The provider returned an error." |
| `timeout` | no complete response in 30 s (browser-side) | "Generation timed out." |
| `refused` | the model declined or was blocked by the provider | "The model didn't produce CSS for this request." |
| `malformed-response` | not the JSON contract | "The response wasn't usable CSS." |
| `unsafe-output` | §4 rule hit (`!important`, `url()`, disallowed property, …) | "The generated CSS was rejected: *reason*." The rejected text is shown read-only; it is not put in the draft. |
| `no-css` | zero valid declarations | as above |
| `cancelled` | the user pressed Cancel | "Generation cancelled." |
| `superseded` | target change, new Generate, pane disposed | "The target changed, so generation was cancelled." |

**Cancellation:**
- **One request at a time** per DevTools window. A new Generate, a target change or pane disposal sends `avoraStyleAbort(id)`.
- **The abort reaches the network.** The service destroys the `SimpleURLLoader`, which cancels it, unlike upstream AIDA.
- **Stale replies are ignored.** The frontend drops any reply whose id isn't the current one.
- **Bindings destroyed** (DevTools closed): all of that window's loaders are cancelled.

## 8. Checkpoint 3B implementation plan

1. **Browser service** (`src/chrome/browser/avora/style_ai/`):
   - `AvoraStyleAiKeyStore` on the `LiveFolderCredentialStore` pattern;
   - `AvoraStyleAiService` (per profile; not created off the record unless decided);
   - one provider adapter behind a small `StyleAiProvider` interface: request mapping, auth header, response text extraction, error mapping;
   - request-schema validator, 30 s timeout, single retry on 5xx/429, cancellation, traffic annotation.
   - **Tests:** unit tests for schema validation, caps, error mapping, key-store encrypt/refuse paths, and cancel.
2. **Embedder messages**, the first Avora overlay of `chrome/browser/devtools`, kept minimal:
   - `avoraStyleAiStatus(cb)` → `{configured, consented, available, reason}`;
   - `avoraStyleGenerate(requestJson, cb)` → `{id}`, then a result callback;
   - `avoraStyleAbort(id)`;
   - the frontend `InspectorFrontendHostAPI`, `devtools_compatibility.js` and stub, overlaid whole like `CSSModel.ts`.
3. **Settings UI:** an Avora Settings section, "Style Studio AI", covering provider, key (write-only), remove, consent reset, and a link to the provider's terms.
4. **Frontend:**
   - `StyleStudioAiContext.ts`: pure context builder with allowlists, caps and scrubbing;
   - `StyleStudioAiOutput.ts`: CSS validator on top of `parseDeclarations`;
   - `StyleStudioAiClient.ts`: host calls, request ids, abort;
   - UI in `StyleStudioView.ts`: instruction field, Generate/Cancel, replace confirmation, consent and set-up states;
   - wiring in `StyleIntelligencePane.ts` that cancels on target change.
5. **Tests:**
   - unit tests for the context builder (no text, no URLs, caps, scrubbing), the output validator (JS, HTML, rules, `url()`, `!important`, oversize, injection-style notes), and every UI state;
   - browser unit tests;
   - a live verifier against a **local fake provider** (an HTTP fixture), using a test-only endpoint override (open decision 6). It covers: generate → draft → explicit apply; the replace confirmation; cancel; a target change during generation; each error; and that nothing is applied automatically.
6. **Docs:** a user-facing privacy note plus the checkpoint findings.

## 9. Open decisions for approval

1. **First provider and default model.** The design is provider-agnostic behind one adapter.
   - **Recommended:** Anthropic's Messages API (the Claude 5 family, a fast default such as `claude-sonnet-5-5`, user-selectable).
   - **Alternatives:** OpenAI, or Gemini through the public API with a user key.
2. **Where the key is entered.** Recommended: Avora Settings, not DevTools.
3. **Off-the-record profiles:** disabled (recommended) or allowed.
4. **Context breadth.** Is the parent/ancestor summary and the ~60-property allowlist acceptable, or should the first release send target-only data?
5. **`url()` in generated CSS.** Rejected in v1 (recommended) or allowed with an explicit warning.
6. **Test-only endpoint override** (a command-line switch compiled into non-official builds only) for the fake-provider verifier.
7. **Consent and disclosure text,** and whether Avora should recommend provider zero-retention settings.
8. **Overlaying `chrome/browser/devtools/` files.** This is a new maintenance surface on every Chromium roll; the alternative is not to have a DevTools-integrated feature.
9. **Limits.** Timeout 30 s, request ≤ 16 KiB, response ≤ 32 KiB, ≤ 40 declarations, instruction ≤ 1,000 characters.
10. **`!important` in model output:** reject the whole result (recommended, as specified) or strip it with a warning.
