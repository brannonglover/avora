# macOS Web Authentication Sessions

When another app signs a user in with `ASWebAuthenticationSession` — the native
sheet that reads *"App Name" Wants to Use "example.com" to Sign In* — macOS
hands the session to the **default browser** rather than opening a URL. If Avora
is the default browser, that handoff lands in Avora.

**On a local `out/` build the handoff never completes.** Clicking Continue does
nothing at all: no window, no error, and the calling app's completion handler is
never invoked. This is expected, it is not an Avora defect, and it is not worth
re-debugging. The cause is code signing, documented below.

## How the handoff works

The path has nothing to do with URL opens, and therefore nothing to do with the
mini window. `ShowMiniWindow` is reached only from
`OpenStartupTabsInBrowserWithProfile` in `app_controller_mac.mm`, which handles
Apple Event URL opens. Authentication sessions take a separate route:

```
SafariLaunchAgent enqueues the request
  → LaunchServices opens the default browser with LAUNCHED_FOR_AUTHENTICATION_SESSION=YES
    and the 'sfri'/'auth' Apple Event
  → AuthenticationServices.framework, inside the browser:
      -[ASWebAuthenticationSessionWebBrowserSessionManager _fetchNextAvailableRequest]
      → -[ASWebAuthenticationSessionController noteNewRequestsAreAvailable]
      → -[ASWebAuthenticationSessionRequestClient listenForRequests]
  → -[AppController beginHandlingWebAuthenticationSessionRequest:]
  → AuthSessionRequest::StartNewAuthSession   (chrome/browser/mac/auth_session_request.mm)
  → AuthSessionRequest::CreateBrowser         → a TYPE_POPUP browser window
```

The popup window type is deliberate upstream and carries a security comment: the
window **must** show a location indicator the user cannot type into. Do not
reroute this to the mini window without resolving that constraint — the mini
window's address chip is editable.

Avora does not overlay `chrome/browser/mac/auth_session_request.{h,mm}` at all,
and its `app_controller_mac.mm` overlay does not touch any auth-session code.
This entire path is stock Chromium.

## The signing requirement

`SafariLaunchAgent` will only vend a request to a browser whose **code-signing
application identifier** it can read and resolve. Concretely it needs both:

1. The `com.apple.application-identifier` entitlement, whose value is
   `<TeamID>.<bundle id>` — Google Chrome ships `EQHXZ8M8AV.com.google.Chrome`.
2. A LaunchServices application record that resolves that team-prefixed
   identifier.

A locally built Chromium satisfies neither. `out/Default/Avora.app` is
`adhoc, linker-signed` with `TeamIdentifier=not set`, and its signing identifier
is the executable name (`Avora`) rather than the bundle ID. Stock
`out/Default/Chromium.app` from the same build directory is signed identically,
so this affects any local Chromium build, not just Avora.

Three states have been observed, each with its own fault from the daemon:

| Build signature | `SafariLaunchAgent` fault |
| --- | --- |
| ad-hoc, linker-signed (as built) | `Failed to get application identifier when establishing new connection` |
| Apple Development identity, no entitlement | same as above — a Team ID alone is not enough |
| Apple Development identity + `application-identifier` | `Attempted to reconnect to <TeamID>.com.avora.browser but couldn't find matching application record` |

The third row is the useful one: the entitlement **is** what the daemon reads,
and adding it moves the failure from "cannot read an identity" to "identity read,
no matching app record." Re-registering the bundle with `lsregister -f` does not
close that gap; the LaunchServices record is keyed on `com.avora.browser` while
the daemon resolves `<TeamID>.com.avora.browser`. Closing it needs a
provisioning profile that grants the entitlement plus a properly distributed,
notarized application — release engineering, not a source change.

Signing a dev build with an Apple Development certificate to test this also
changes the app's identity for keychain and TCC purposes, so macOS re-prompts
for the Safe Storage keychain item and previously granted permissions.

## Why the failure is completely silent

`-[ASWebAuthenticationSessionRequestClient listenForRequests]` begins with an
early return:

```
if (self->_serverConnection != nil) { os_log("Server is already connected."); return; }
```

When the daemon rejects the connection it tears it down, but the framework
leaves `_serverConnection` populated — it logs `Connection to server invalidated.`
and still does not clear the ivar. Every later request therefore short-circuits
on that dead pointer without even reaching the rejection. The first attempt after
launch fails loudly in the system log; every attempt after that fails silently.

This latch lives entirely in Apple's closed-source framework. It cannot be fixed
from Avora except by writing to a private ivar at a hardcoded offset, which would
break on any macOS update. It is also downstream of the signing rejection: with a
valid identity the connection is not torn down, so the cached connection is live
and the early return is correct. Leave it alone.

## Multiple running instances

Upstream `AppController` registers itself as the session handler unconditionally
in `applicationDidFinishLaunching`:

```objc
ASWebAuthenticationSessionWebBrowserSessionManager.sharedManager.sessionHandler = self;
```

There is no gate on profile, on user data directory, or on owning the process
singleton — and the process singleton is scoped per `--user-data-dir`, so a second
instance with its own data directory is a fully independent browser that runs the
same startup path and registers the same way. Chromium has no cross-process
forwarding for authentication sessions; `process_singleton_mac.mm` forwards only
`kAEGetURL` (URL opens). Nothing in `chrome/` references
`wasLaunchedByAuthenticationServices` or `LAUNCHED_FOR_AUTHENTICATION_SESSION`.

The consequence is that any Chromium-derived process running this startup path
registers as a candidate handler — including test binaries. Browser tests on this
machine were observed opening their own connections to the launch agent.

Which instance actually receives a request is decided entirely outside the
browser: the daemon keys its handler map on the code-signing application
identifier, which is identical across every instance of the same bundle
regardless of profile, and LaunchServices picks the instance to notify
(`_kLSOpenOptionPreferRunningInstanceKey`). Avora behaves exactly as upstream
Chromium here and has not bypassed any routing. If Avora ever wants a stricter
guarantee — for example, refusing to register when `--user-data-dir` was passed,
so automation instances cannot claim the handler — that would be a deliberate
divergence from upstream, not a bug fix.

## Reproducing

A minimal signed-adhoc AppKit app that calls `ASWebAuthenticationSession.start()`
is enough to exercise the path end to end. Watch it with:

```sh
log stream --style compact --predicate 'category == "AuthenticationSession"'
```

and breakpoint `-[AppController beginHandlingWebAuthenticationSessionRequest:]`
plus `AuthSessionRequest::StartNewAuthSession` to confirm whether the request ever
reaches Chromium code.

## Distribution-build verification

Run this once there is a properly provisioned, distribution-signed Avora build.
There is no point running it against an `out/` build — step 1 fails by
construction, and everything downstream fails with it.

**Steps 1–7 all passing is the criterion for closing the auth-session issue.**
Step 8 is observational: it records real behavior for the record and does not
gate closure.

Before starting, confirm no other Avora process is running. A second instance
silently claims the handler and absorbs the request, which invalidates every
result below:

```sh
ps -eo pid,command | grep "[A]vora.app/Contents/MacOS/Avora"
```

Keep this running throughout; every step reads from it:

```sh
log stream --style compact --predicate 'category == "AuthenticationSession"'
```

### 1. Effective `application-identifier` entitlement and Team ID

```sh
codesign -dv --verbose=4 /path/to/Avora.app 2>&1 | grep -iE "^Identifier|TeamIdentifier|Authority"
codesign -d --entitlements - --xml /path/to/Avora.app | plutil -p -
```

Expect `Identifier=com.avora.browser`, a real `TeamIdentifier`, a distribution
authority chain, and `com.apple.application-identifier` equal to
`<TeamID>.com.avora.browser`. An ad-hoc signature (`Signature=adhoc`,
`TeamIdentifier=not set`) fails here.

The entitlement must be *granted*, not merely embedded — signing a local build
with a bare Apple Development certificate embeds it but nothing honors it.
Confirm a provisioning profile is present and actually authorizes it:

```sh
security cms -D -i /path/to/Avora.app/Contents/embedded.provisionprofile
```

Its `Entitlements` dictionary must contain the same `application-identifier`.

### 2. LaunchServices resolves the identity SafariLaunchAgent expects

Confirm the signed bundle is the registered default handler:

```sh
LSR=/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister
"$LSR" -dump | grep -A3 "path:.*Avora.app"
```

The decisive check is negative and comes from the log stream: after triggering a
session, `SafariLaunchAgent` must **not** emit either of

```
Failed to get application identifier when establishing new connection
Attempted to reconnect to <TeamID>.com.avora.browser but couldn't find matching application record
```

Seeing the second one means the entitlement is being read but LaunchServices has
no matching application record — the exact wall the dev-signed build hit.

### 3. Continue reaches `beginHandlingWebAuthenticationSessionRequest:`

With exactly one Avora running, attach and breakpoint:

```sh
lldb -p "$(pgrep -f 'MacOS/Avora$')"
```

```
breakpoint set -r "beginHandlingWebAuthenticationSessionRequest"
breakpoint set -n "AuthSessionRequest::StartNewAuthSession"
breakpoint set -n "AuthSessionRequest::CreateBrowser"
continue
```

Trigger a session and press Continue. The breakpoint must hit. Note that macOS
remembers consent per calling app and origin, so the sheet may stop appearing on
repeat runs — that is normal and not a failure.

### 4. `AuthSessionRequest::StartNewAuthSession` executes

The second breakpoint must hit, on the same request. If step 3 hits and this does
not, the failure is inside `BeginHandlingWebAuthenticationSessionRequestWithProfile`
— most likely profile loading, which is Avora-side and worth investigating.

### 5. The `TYPE_POPUP` authentication window appears

`AuthSessionRequest::CreateBrowser` must hit and a window must appear. Verify it
is genuinely a popup and not an ordinary tab:

- no tab strip, and no Avora sidebar or Spaces bar;
- the location text is visible but **not** editable.

The read-only location indicator is the upstream security requirement. If Avora's
window customization has made the location editable or hidden it in this window
type, that is an Avora-specific regression and must be fixed before closing.

### 6. A real flow returns its result to the initiating app

Steps 3–5 only prove the request arrives. This step proves the round trip. It
needs a provider that actually redirects to the session's callback scheme —
`https://example.com` never redirects and cannot complete a flow.

Sign in for real in the popup and confirm:

- the popup closes itself on the callback redirect;
- the initiating app's completion handler receives the callback URL, with no
  error (the harness in *Reproducing* logs `CALLBACK url=… err=nil`);
- `AuthNavigationThrottle` cancelled the callback navigation rather than letting
  the popup navigate to it.

### 7. Repeat runs stay healthy

Run the full flow at least three times in one browser session, without
restarting Avora. Every run must reach step 3. The failure signature to watch for
is a run where the log shows

```
Received notification for new request.
Server is already connected.
```

and then nothing — that is the stale-latch state, and it means the connection was
torn down and not replaced. On a correctly signed build it must not occur.
`Server is already connected.` on its own is fine and expected; it is only a
problem when no dispatch follows.

If you want to inspect the ivar directly, the offsets used during the original
investigation were `manager+0x8` → controller, `controller+0x18` → request
client, `client+0x18` → `_serverConnection`. These are private and version
specific; re-derive them before trusting them on a newer macOS.

### 8. Second instance with a different `--user-data-dir` (observational)

With the first instance still running, start a second:

```sh
/path/to/Avora.app/Contents/MacOS/Avora --user-data-dir=/tmp/avora-authtest
```

Trigger a session and record, from the log stream, **which pid** receives
`Received notification for new request.` and which instance opens the popup.

Document the result here. Do not change the registration behavior on the
strength of this step alone: registering unconditionally is upstream Chromium
behavior, the daemon keys its handler map on the code-signing identity (identical
across instances), and LaunchServices chooses the instance. Only an
Avora-specific failure — a request reaching an Avora instance and being dropped
rather than handled — justifies diverging from upstream.
