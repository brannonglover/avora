#!/bin/bash
# Packages the built Avora.app into the DMG the site's download buttons serve,
# and saves it as a draft in website/releases.json with its size, checksum,
# notarization and update signature. Nothing goes out until the draft is
# published at /admin/releases.
#
#   npm run dmg            # from website/; uses chromium/src/out/Default
#   npm run dmg -- <out>   # another build directory, e.g. out/Signing
#
# With no CODESIGN_IDENTITY the copy is signed ad hoc. To release it, also set:
#
#   CODESIGN_IDENTITY     a Developer ID Application identity
#   PROVISIONING_PROFILE  a Developer ID profile for the app, which must list that
#                         identity's certificate
#   NOTARY_PROFILE        a name saved with `xcrun notarytool store-credentials`
#
# A Developer ID build is notarized and stapled before anything is published. An
# Apple Development identity gets the same signing, for testing on a Mac the
# profile lists, and no notarization.
#
# Either way the signature is redone on a staged copy, because a build
# directory's app is usually out of step with its own signature.
#
# Sparkle only goes in once website/sparkle-public-key exists, so a build made
# without one simply never checks for updates. A Developer ID build requires it,
# because a release without an updater can't be told about the next one. The
# matching private key is read from the Keychain, where generate_keys left it.
set -euo pipefail

SITE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO_DIR="$(cd "$SITE_DIR/.." && pwd)"
OUT_NAME="${1:-out/Default}"
APP="$REPO_DIR/chromium/src/$OUT_NAME/Avora.app"
IDENTITY="${CODESIGN_IDENTITY:--}"
CHROMIUM_ENTITLEMENTS="$REPO_DIR/chromium/src/chrome/app"

die() {
  echo "$1" >&2
  exit 1
}

[[ -d "$APP" ]] || die "No Avora.app at $APP. Run npm run build first."

VERSION="$(node -p "require('$REPO_DIR/package.json').version")"
CHROMIUM="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")"
BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")"
FILE="Avora-$VERSION-arm64.dmg"
DEST_DIR="$SITE_DIR/public/downloads"
DEST="$DEST_DIR/$FILE"
FEED_URL="https://www.avorabrowser.com/appcast.xml"
PUBLIC_KEY_FILE="$SITE_DIR/sparkle-public-key"
SPARKLE="$SITE_DIR/.sparkle"
# Six hours, so a security fix reaches people the day it ships.
CHECK_INTERVAL=21600

# A published release is on people's Macs under this version and signature, and
# Sparkle would never offer a rebuild of it to anyone who already has it.
STATUS="$(node -p '
  const [path, version] = process.argv.slice(1);
  JSON.parse(require("fs").readFileSync(path, "utf8"))
    .find((release) => release.version === version)?.status ?? "new"
' "$SITE_DIR/releases.json" "$VERSION")"
[[ "$STATUS" != "published" ]] ||
  die "$VERSION is already published. Bump the version in package.json first."

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/avora-dmg.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

STAGED_APP="$STAGE/root/Avora.app"
DMG="$STAGE/$FILE"
NOTARIZED=false

# Sets TEAM_ID. Notarization doesn't catch a profile that doesn't fit: the app
# passes, then macOS kills it at launch for an entitlement nothing granted.
check_profile() {
  [[ -f "${PROVISIONING_PROFILE:-}" ]] ||
    die "Set PROVISIONING_PROFILE to the profile for $BUNDLE_ID."

  local profile="$STAGE/profile.plist" granted hash cert i=0
  security cms -D -i "$PROVISIONING_PROFILE" > "$profile"
  TEAM_ID="$(/usr/libexec/PlistBuddy -c 'Print :TeamIdentifier:0' "$profile")"
  granted="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.application-identifier' "$profile")"
  [[ "$granted" == "$TEAM_ID.$BUNDLE_ID" ]] ||
    die "The profile is for $granted, not $TEAM_ID.$BUNDLE_ID."

  # find-certificate can pick an expired namesake, so take the hash from here.
  hash="$(security find-identity -v -p codesigning |
    awk -v identity="$IDENTITY" 'index($0, identity) { print $2; exit }')"
  [[ -n "$hash" ]] || die "No valid identity \"$IDENTITY\" in the Keychain."

  while cert="$(plutil -extract "DeveloperCertificates.$i" raw -o - "$profile" 2>/dev/null)"; do
    [[ "$(base64 -D <<< "$cert" | shasum -a 1 | cut -d' ' -f1 | tr a-f A-F)" == "$hash" ]] && return
    i=$((i + 1))
  done
  die "The profile doesn't list \"$IDENTITY\". A Developer ID needs a Developer ID profile, not a development one."
}

# Hardened runtime blocks the camera, microphone and so on unless the app
# declares them, so Avora's entitlement joins Chromium's, as it does in
# src/chrome/BUILD.gn.
write_entitlements() {
  sed -e "s/\${CHROMIUM_TEAM_ID}/$TEAM_ID/" -e "s/\${CHROMIUM_BUNDLE_ID}/$BUNDLE_ID/" \
    "$REPO_DIR/src/chrome/app/app-entitlements-avora.plist" > "$STAGE/avora-entitlements.plist"
  cp "$CHROMIUM_ENTITLEMENTS/app-entitlements.plist" "$STAGE/app-entitlements.plist"
  /usr/libexec/PlistBuddy -c "Merge $STAGE/avora-entitlements.plist" "$STAGE/app-entitlements.plist"
}

# Avora's own version lives in CFBundleVersion, which Sparkle compares against the
# feed. CFBundleShortVersionString has to stay Chromium's, because app shims use
# it to find the versioned framework inside the bundle.
stamp_version() {
  /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $VERSION" \
    "$STAGED_APP/Contents/Info.plist"
}

# Sets UPDATES, and adds Sparkle to the staged app. The XPC services are only for
# sandboxed apps, which the browser process isn't.
embed_sparkle() {
  local plist="$STAGED_APP/Contents/Info.plist"
  local framework="$STAGED_APP/Contents/Frameworks/Sparkle.framework"

  UPDATES=false
  if [[ ! -f "$PUBLIC_KEY_FILE" ]]; then
    [[ "$RELEASE" == false ]] ||
      die "A release needs $PUBLIC_KEY_FILE. See \"Before this goes public\" in the README."
    return
  fi

  bash "$SITE_DIR/scripts/fetch-sparkle.sh"
  ditto "$SPARKLE/Sparkle.framework" "$framework"
  rm -rf "$framework/XPCServices" "$framework/Versions/B/XPCServices"

  /usr/libexec/PlistBuddy \
    -c "Add :SUFeedURL string $FEED_URL" \
    -c "Add :SUPublicEDKey string $(cat "$PUBLIC_KEY_FILE")" \
    -c "Add :SUEnableAutomaticChecks bool true" \
    -c "Add :SUScheduledCheckInterval integer $CHECK_INTERVAL" \
    -c "Add :SUVerifyUpdateBeforeExtraction bool true" \
    "$plist"
  UPDATES=true
}

# sign <path> <identifier> [options] [entitlements]
sign() {
  local args=(--force --timestamp --sign "$IDENTITY" --identifier "$2")
  [[ -z "${3:-}" ]] || args+=(--options "$3")
  [[ -z "${4:-}" ]] || args+=(--entitlements "$4")
  codesign "${args[@]}" "$1"
}

# Signs inside out, with the same parts, identifiers, options and entitlements
# as Chromium's chrome/installer/mac/signing/parts.py. The renderer and GPU
# helpers go without library validation, which their JIT entitlement rules out.
sign_inside_out() {
  local hardened=restrict,library,kill,runtime
  local jit=restrict,kill,runtime
  local renderer="$CHROMIUM_ENTITLEMENTS/helper-renderer-entitlements.plist"
  local gpu="$CHROMIUM_ENTITLEMENTS/helper-gpu-entitlements.plist"
  local alerts="$BUNDLE_ID.framework.AlertNotificationService"
  local framework="$STAGED_APP/Contents/Frameworks/Avora Framework.framework"
  local helpers="$framework/Helpers" libraries="$framework/Libraries"

  sign "$helpers/chrome_crashpad_handler" chrome_crashpad_handler "$hardened"
  sign "$helpers/Avora Helper.app" "$BUNDLE_ID.helper" "$hardened"
  sign "$helpers/Avora Helper (Renderer).app" "$BUNDLE_ID.helper.renderer" "$jit" "$renderer"
  sign "$helpers/Avora Helper (GPU).app" "$BUNDLE_ID.helper" "$jit" "$gpu"
  sign "$helpers/Avora Helper (Aperitif).app" "$BUNDLE_ID.helper" "$hardened"
  sign "$helpers/Avora Helper (Aperitif Renderer).app" "$BUNDLE_ID.helper.renderer" "$jit" "$renderer"
  sign "$helpers/Avora Helper (Aperitif GPU).app" "$BUNDLE_ID.helper" "$jit" "$gpu"
  sign "$helpers/Avora Helper (Aperitif Alerts).app" "$alerts" "$hardened"
  sign "$helpers/Avora Helper (Alerts).app" "$alerts" "$hardened"
  sign "$helpers/app_mode_loader" app_mode_loader "$hardened"
  sign "$helpers/web_app_shortcut_copier" "$BUNDLE_ID.web_app_shortcut_copier" "$hardened"
  sign "$libraries/libaperitif.dylib" libaperitif
  sign "$libraries/libvk_swiftshader.dylib" libvk_swiftshader
  sign "$libraries/libvulkan.dylib" libvulkan
  sign "$framework" "$BUNDLE_ID.framework"

  if [[ "$UPDATES" == true ]]; then
    local sparkle="$STAGED_APP/Contents/Frameworks/Sparkle.framework"
    sign "$sparkle/Versions/B/Autoupdate" org.sparkle-project.Sparkle.Autoupdate runtime
    sign "$sparkle/Versions/B/Updater.app" org.sparkle-project.Sparkle.Updater runtime
    sign "$sparkle" org.sparkle-project.Sparkle runtime
  fi

  cp "$PROVISIONING_PROFILE" "$STAGED_APP/Contents/embedded.provisionprofile"
  sign "$STAGED_APP" "$BUNDLE_ID" "$hardened" "$STAGE/app-entitlements.plist"
}

notarize() {
  echo "Submitting to Apple's notary service"
  local result id status
  result="$(xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" \
    --wait --output-format json)" || true
  id="$(node -p 'JSON.parse(process.argv[1]).id' "$result")"
  status="$(node -p 'JSON.parse(process.argv[1]).status' "$result")"
  if [[ "$status" != "Accepted" ]]; then
    xcrun notarytool log "$id" --keychain-profile "$NOTARY_PROFILE" >&2
    die "Notarization finished as \"$status\". Nothing was published."
  fi

  xcrun stapler staple "$DMG"
  xcrun stapler validate "$DMG"
  spctl -a -t open --context context:primary-signature -vv "$DMG"
  spctl -a -vv "$STAGED_APP"
  NOTARIZED=true
}

RELEASE=false
[[ "$IDENTITY" != "Developer ID Application:"* ]] || RELEASE=true

if [[ "$IDENTITY" != "-" ]]; then
  check_profile
  write_entitlements
fi
if [[ "$RELEASE" == true ]]; then
  [[ -n "${NOTARY_PROFILE:-}" ]] ||
    die "Set NOTARY_PROFILE to a name saved with xcrun notarytool store-credentials."
  xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" > /dev/null
fi

echo "Staging $APP"
mkdir "$STAGE/root"
ditto "$APP" "$STAGED_APP"
ln -s /Applications "$STAGE/root/Applications"
stamp_version
embed_sparkle

echo "Signing with identity: $IDENTITY"
if [[ "$IDENTITY" == "-" ]]; then
  codesign --force --deep --sign "$IDENTITY" \
    --preserve-metadata=entitlements,flags "$STAGED_APP"
else
  sign_inside_out
fi
codesign --verify --deep --strict "$STAGED_APP"

echo "Compressing"
hdiutil create -quiet -volname "Avora" -srcfolder "$STAGE/root" \
  -fs APFS -format ULFO -ov "$DMG"

if [[ "$IDENTITY" != "-" ]]; then
  codesign --force --timestamp --sign "$IDENTITY" "$DMG"
fi
if [[ "$RELEASE" == true ]]; then
  notarize
fi

mkdir -p "$DEST_DIR"
mv -f "$DMG" "$DEST"

SIZE="$(stat -f %z "$DEST")"
SHA256="$(shasum -a 256 "$DEST" | cut -d' ' -f1)"

# Signed last, because this has to cover the stapled DMG exactly as it's served.
SIGNATURE=""
if [[ "$UPDATES" == true ]]; then
  SIGNATURE="$("$SPARKLE/bin/sign_update" -p "$DEST")"
fi

# A starting point for the notes: what changed since the last published release.
LAST_VERSION="$(node -p '
  JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"))
    .filter((release) => release.status === "published")
    .map((release) => release.version)
    .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }))[0]
' "$SITE_DIR/releases.json")"
NOTES="$(git -C "$REPO_DIR" log --format=%s "v$LAST_VERSION..HEAD" 2>/dev/null || true)"

node -e '
  const fs = require("fs");
  const [path, version, chromium, file, sizeBytes, sha256, notarized, signature, notes] =
    process.argv.slice(1);
  const releases = JSON.parse(fs.readFileSync(path, "utf8"));
  const existing = releases.find((release) => release.version === version);
  // Rebuilding a draft replaces the build, not the notes written for it.
  const draft = {
    version,
    chromium,
    file,
    sizeBytes: Number(sizeBytes),
    sha256,
    notarized: notarized === "true",
    edSignature: signature || null,
    title: existing?.title ?? null,
    bullets: existing?.bullets.length ? existing.bullets : notes.split("\n").filter(Boolean),
    status: "draft",
    createdAt: new Date().toISOString(),
    publishedAt: null,
  };
  const next = [draft, ...releases.filter((release) => release !== existing)].sort(
    (a, b) => b.version.localeCompare(a.version, undefined, { numeric: true }),
  );
  fs.writeFileSync(path, JSON.stringify(next, null, 2) + "\n");
' "$SITE_DIR/releases.json" "$VERSION" "$CHROMIUM" "$FILE" "$SIZE" "$SHA256" \
  "$NOTARIZED" "$SIGNATURE" "$NOTES"

echo "Wrote $DEST ($((SIZE / 1000000)) MB)"
if [[ "$UPDATES" == false ]]; then
  echo "This build has no updater, because $PUBLIC_KEY_FILE doesn't exist."
fi
