#!/usr/bin/env bash
# Takes the version in package.json as far as it can go without a person: builds
# it, signs and notarizes it, uploads the DMG, and opens the admin with the draft
# notes. Pressing Publish there sends it out.
#
#   npm version minor
#   npm run release
#
# Everything that can fail quickly is checked before the build, which is the slow
# part. If something fails after packaging, running this again reuses the DMG.
#
# Defaults that can be overridden from the environment: CODESIGN_IDENTITY, the
# Developer ID in the Keychain; PROVISIONING_PROFILE; NOTARY_PROFILE.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="$ROOT/website"
REPO=brannonglover/avora
ADMIN_URL=http://localhost:3000/admin/releases
PUBLIC_KEY_FILE="$SITE/sparkle-public-key"
GENERATE_KEYS="$SITE/.sparkle/bin/generate_keys"

cd "$ROOT"

die() {
  echo "$1" >&2
  exit 1
}

step() {
  echo "==> $1"
}

VERSION="$(node -p "require('./package.json').version")"
TAG="v$VERSION"
FILE="Avora-$VERSION-arm64.dmg"
DMG="$SITE/public/downloads/$FILE"

export NOTARY_PROFILE="${NOTARY_PROFILE:-avora-notary}"
export PROVISIONING_PROFILE="${PROVISIONING_PROFILE:-$HOME/Downloads/Avora_Developer_ID.provisionprofile}"
export CODESIGN_IDENTITY="${CODESIGN_IDENTITY:-$(security find-identity -v -p codesigning |
  sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' | head -n 1)}"

# This version's row in releases.json, or empty.
release_field() {
  node -p '
    const [path, version, field] = process.argv.slice(1);
    JSON.parse(require("fs").readFileSync(path, "utf8"))
      .find((release) => release.version === version)?.[field] ?? ""
  ' "$SITE/releases.json" "$VERSION" "$1"
}

check_ready() {
  step "Checking Avora $VERSION is ready to release"

  [[ "$(git branch --show-current)" == main ]] || die "Releases come from main."

  # The build takes what's on disk, so anything uncommitted would ship without
  # being in the tag.
  local changes
  changes="$(git status --porcelain -- . \
    ':(exclude)website/releases.json' ':(exclude)website/sparkle-public-key')"
  [[ -z "$changes" ]] || die "Commit these first, or they'd ship in the build:
$changes"

  git fetch -q origin main
  git merge-base --is-ancestor origin/main HEAD ||
    die "GitHub has commits this checkout doesn't. Pull first."

  [[ "$(release_field status)" != published ]] ||
    die "$VERSION is already published. Run npm version minor first."

  gh auth status > /dev/null 2>&1 || die "GitHub CLI isn't signed in. Run gh auth login."
  [[ -n "$CODESIGN_IDENTITY" ]] || die "No Developer ID Application identity in the Keychain."
  [[ -f "$PROVISIONING_PROFILE" ]] ||
    die "No provisioning profile at $PROVISIONING_PROFILE. Set PROVISIONING_PROFILE."
  xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" > /dev/null ||
    die "No notary credentials named $NOTARY_PROFILE. Set NOTARY_PROFILE."
}

# Installed copies trust the public key they shipped with, so the key in the
# repo and the one that signs have to be the same pair.
ensure_update_key() {
  bash "$SITE/scripts/fetch-sparkle.sh"

  local key
  key="$("$GENERATE_KEYS" -p 2> /dev/null || true)"
  if [[ -z "$key" ]]; then
    [[ ! -f "$PUBLIC_KEY_FILE" ]] ||
      die "The update signing key isn't in this Keychain. Restore it with generate_keys -f."
    step "Creating the update signing key"
    "$GENERATE_KEYS" > /dev/null
    key="$("$GENERATE_KEYS" -p)"
  fi

  if [[ -f "$PUBLIC_KEY_FILE" && "$(cat "$PUBLIC_KEY_FILE")" != "$key" ]]; then
    die "The Keychain's update key doesn't match $PUBLIC_KEY_FILE, so installed copies would reject this build."
  fi

  if [[ ! -f "$PUBLIC_KEY_FILE" ]]; then
    echo "$key" > "$PUBLIC_KEY_FILE"
    cat <<EOF

Back up the update signing key. If it's lost, installed copies can never be
updated again. This writes it to a file; put that in 1Password, then delete it:

  $GENERATE_KEYS -x ~/Desktop/avora-update-key.txt

EOF
  fi

  if [[ -n "$(git status --porcelain -- "$PUBLIC_KEY_FILE")" ]]; then
    git add "$PUBLIC_KEY_FILE"
    git commit -q -m "Add the public key that installed copies verify updates with." \
      -- "$PUBLIC_KEY_FILE"
  fi
}

# The tag has to name what's being built. One that's still local can follow HEAD;
# one already on GitHub stays where it is.
prepare_tag() {
  local remote
  remote="$(git ls-remote origin "refs/tags/$TAG^{}" | cut -f1)"
  [[ -n "$remote" ]] || remote="$(git ls-remote origin "refs/tags/$TAG" | cut -f1)"

  if [[ -z "$remote" ]]; then
    git tag -f -m "$VERSION" "$TAG" HEAD > /dev/null
  elif [[ "$remote" != "$(git rev-parse HEAD)" ]]; then
    die "$TAG is already on GitHub at another commit. Run npm version patch."
  fi
}

# A notarized DMG of this exact commit, left by a run that failed later on.
already_packaged() {
  [[ "$(release_field status)" == draft &&
    "$(release_field gitSha)" == "$(git rev-parse HEAD)" &&
    "$(release_field notarized)" == true &&
    -f "$DMG" &&
    "$(shasum -a 256 "$DMG" | cut -d' ' -f1)" == "$(release_field sha256)" ]]
}

build_and_package() {
  if already_packaged; then
    step "Reusing the notarized DMG of this commit"
    return
  fi
  step "Building Avora"
  npm run build
  step "Signing and notarizing"
  npm --prefix "$SITE" run dmg
}

upload() {
  step "Uploading to GitHub"
  git push origin main "refs/tags/$TAG"
  if gh release view "$TAG" --repo "$REPO" > /dev/null 2>&1; then
    gh release upload "$TAG" "$DMG" --repo "$REPO" --clobber
  else
    gh release create "$TAG" "$DMG" --repo "$REPO" --title "Avora $VERSION" \
      --notes "" --verify-tag
  fi
}

open_admin() {
  step "Avora $VERSION is ready. Write the notes and press Publish at $ADMIN_URL"
  if curl -fso /dev/null "$ADMIN_URL"; then
    open "$ADMIN_URL"
    return
  fi

  (
    for _ in {1..30}; do
      if curl -fso /dev/null "$ADMIN_URL"; then
        open "$ADMIN_URL"
        break
      fi
      sleep 1
    done
  ) &
  exec npm --prefix "$SITE" run dev
}

check_ready
ensure_update_key
prepare_tag

# A build and notarization can outlast the display sleep timer.
caffeinate -i -w $$ &

build_and_package
upload
open_admin
