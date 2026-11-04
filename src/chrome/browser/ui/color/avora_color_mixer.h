// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COLOR_AVORA_COLOR_MIXER_H_
#define CHROME_BROWSER_UI_COLOR_AVORA_COLOR_MIXER_H_

#include "ui/color/color_provider_key.h"

namespace avora {
struct SpaceTheme;
}

namespace ui {
class ColorProvider;
}

// Adds Avora-specific color overrides to |provider|: the default palette, for
// windows whose Space has no theme.
void AddAvoraColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key);

// The mixer supplier for a window showing a Space with |theme|, to set as the
// window's ColorProviderKey::app_controller.  Null for an empty theme, which
// the default mixer above already covers.
//
// Equal themes share one supplier, and a supplier lives until
// ReleaseAvoraSpaceThemeSuppliers(): the color provider cache tells keys apart
// by the supplier's address, so an address must never be reused for another
// theme while providers built from it are cached.
ui::ColorProviderKey::InitializerSupplier* GetAvoraSpaceThemeSupplier(
    const avora::SpaceTheme& theme);

// Clears the color provider cache, then frees every supplier.  Callers must
// then have each window recompute its key (a native theme update does), which
// asks for suppliers afresh.  For after the Theme Studio closes, which may
// have made a supplier for every spot a drag passed through.
void ReleaseAvoraSpaceThemeSuppliers();

#endif  // CHROME_BROWSER_UI_COLOR_AVORA_COLOR_MIXER_H_
