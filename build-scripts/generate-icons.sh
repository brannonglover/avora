#!/usr/bin/env bash
# Regenerates every Avora branding image under src/chrome/app/theme from the
# master artwork in branding/. Run this after changing branding/avora_icon_1024.png,
# then `build.sh copy-sources` to push the results into the Chromium tree.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AVORA_ROOT="$(dirname "$SCRIPT_DIR")"
MASTER="$AVORA_ROOT/branding/avora_icon_1024.png"
THEME="$AVORA_ROOT/src/chrome/app/theme"
BRANDED="$THEME/chromium"
COMPONENTS="$AVORA_ROOT/src/components/resources"

if [ ! -f "$MASTER" ]; then
  echo "ERROR: $MASTER not found."
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

TOOL="$WORK/icon-tool"
echo "==> Building icon-tool..."
swiftc -O -o "$TOOL" "$SCRIPT_DIR/icon-tool.swift"

# macOS reserves a 100px margin on each side of the 1024px canvas so app icons
# line up with the rest of the Dock. In-product logos stay full bleed.
MAC_MASTER="$WORK/mac_master.png"
"$TOOL" inset "$MASTER" "$MAC_MASTER" 1024 100

echo "==> Generating app.icns..."
ICONSET="$WORK/app.iconset"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
  "$TOOL" resize "$MAC_MASTER" "$ICONSET/icon_${size}x${size}.png" "$size"
  "$TOOL" resize "$MAC_MASTER" "$ICONSET/icon_${size}x${size}@2x.png" "$((size * 2))"
done
mkdir -p "$BRANDED/mac"
iconutil -c icns --output "$BRANDED/mac/app.icns" "$ICONSET"

echo "==> Generating Assets.xcassets..."
XCASSETS="$BRANDED/mac/Assets.xcassets"
rm -rf "$XCASSETS"
mkdir -p "$XCASSETS/AppIcon.appiconset" "$XCASSETS/Icon.iconset"

for size in 16 32 64 128 256 512 1024; do
  "$TOOL" resize "$MAC_MASTER" "$XCASSETS/AppIcon.appiconset/appicon_${size}.png" "$size"
done
"$TOOL" resize "$MAC_MASTER" "$XCASSETS/Icon.iconset/icon_256x256.png" 256
"$TOOL" resize "$MAC_MASTER" "$XCASSETS/Icon.iconset/icon_256x256@2x.png" 512

cat > "$XCASSETS/Contents.json" <<'JSON'
{
  "info" : {
    "author" : "avora",
    "version" : 1
  }
}
JSON

{
  echo '{'
  echo '  "images" : ['
  first=1
  for entry in "16x16 1x 16" "16x16 2x 32" "32x32 1x 32" "32x32 2x 64" \
               "128x128 1x 128" "128x128 2x 256" "256x256 1x 256" "256x256 2x 512" \
               "512x512 1x 512" "512x512 2x 1024"; do
    set -- $entry
    [ "$first" = 1 ] || echo '    },'
    first=0
    echo '    {'
    echo "      \"filename\" : \"appicon_$3.png\","
    echo '      "idiom" : "mac",'
    echo "      \"scale\" : \"$2\","
    echo "      \"size\" : \"$1\""
  done
  echo '    }'
  echo '  ],'
  echo '  "info" : {'
  echo '    "author" : "avora",'
  echo '    "version" : 1'
  echo '  }'
  echo '}'
} > "$XCASSETS/AppIcon.appiconset/Contents.json"

# chrome/BUILD.gn bundles a prebuilt Assets.car, so compile the catalog here
# rather than during the Chromium build.
echo "==> Compiling Assets.car..."
CAR_OUT="$WORK/car"
mkdir -p "$CAR_OUT"
xcrun actool "$XCASSETS" \
  --compile "$CAR_OUT" \
  --platform macosx \
  --minimum-deployment-target 11.0 \
  --app-icon AppIcon \
  --output-partial-info-plist "$WORK/partial.plist" \
  --errors --warnings > /dev/null
cp "$CAR_OUT/Assets.car" "$BRANDED/mac/Assets.car"

echo "==> Generating product logos..."
for size in 16 24 48 64 128 256; do
  "$TOOL" resize "$MASTER" "$BRANDED/product_logo_${size}.png" "$size"
done
# Status tray icons are template images: black glyph, luminance as alpha.
"$TOOL" mono "$MASTER" "$BRANDED/product_logo_22_mono.png" 22

mkdir -p "$THEME/default_100_percent/chromium" "$THEME/default_200_percent/chromium"
"$TOOL" resize "$MASTER" "$THEME/default_100_percent/chromium/product_logo_16.png" 16
"$TOOL" resize "$MASTER" "$THEME/default_100_percent/chromium/product_logo_32.png" 32
"$TOOL" resize "$MASTER" "$THEME/default_200_percent/chromium/product_logo_16.png" 32
"$TOOL" resize "$MASTER" "$THEME/default_200_percent/chromium/product_logo_32.png" 64

echo "==> Generating chrome://version assets..."
mkdir -p "$COMPONENTS/default_100_percent/chromium" "$COMPONENTS/default_200_percent/chromium"
"$TOOL" resize "$MASTER" "$COMPONENTS/default_100_percent/chromium/favicon_product.png" 16
"$TOOL" resize "$MASTER" "$COMPONENTS/default_200_percent/chromium/favicon_product.png" 32

# grit requires the 2x asset to be exactly twice the width of the 1x one, so
# reuse the width the tool measured at 1x.
for variant in "product_logo.png 3c4043" "product_logo_white.png ffffff"; do
  set -- $variant
  width="$("$TOOL" wordmark "$MASTER" "$COMPONENTS/default_100_percent/chromium/$1" 32 "$2")"
  "$TOOL" wordmark "$MASTER" "$COMPONENTS/default_200_percent/chromium/$1" 64 "$2" \
    "$((width * 2))" > /dev/null
done

echo "==> Generating product_logo.svg..."
"$TOOL" resize "$MASTER" "$WORK/logo_512.png" 256
DATA="$(base64 < "$WORK/logo_512.png" | tr -d '\n')"
for name in product_logo.svg product_logo_animation.svg; do
  cat > "$BRANDED/$name" <<SVG
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 256 256" width="256" height="256"><image width="256" height="256" xlink:href="data:image/png;base64,$DATA"/></svg>
SVG
done

echo "==> Done. Branding assets written under src/chrome/app/theme."
