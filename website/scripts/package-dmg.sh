#!/bin/bash
# Packages the built Avora.app into the DMG the site's download buttons serve,
# and records its size, checksum and notarization in website/release.json.
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

node -e '
  const fs = require("fs");
  const path = process.argv[1];
  const release = JSON.parse(fs.readFileSync(path, "utf8"));
  Object.assign(release, {
    version: process.argv[2],
    chromium: process.argv[3],
    file: process.argv[4],
    sizeBytes: Number(process.argv[5]),
    sha256: process.argv[6],
    notarized: process.argv[7] === "true",
  });
  fs.writeFileSync(path, JSON.stringify(release, null, 2) + "\n");
' "$SITE_DIR/release.json" "$VERSION" "$CHROMIUM" "$FILE" "$SIZE" "$SHA256" "$NOTARIZED"

echo "Wrote $DEST ($((SIZE / 1000000)) MB)"
