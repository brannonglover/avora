// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COLOR_AVORA_COLOR_MIXER_H_
#define CHROME_BROWSER_UI_COLOR_AVORA_COLOR_MIXER_H_

#include "ui/color/color_provider_key.h"

namespace ui {
class ColorProvider;
}

// Adds Avora-specific color overrides to |provider|.
void AddAvoraColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key);

#endif  // CHROME_BROWSER_UI_COLOR_AVORA_COLOR_MIXER_H_
