// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as i18n from '../../core/i18n/i18n.js';
import * as UI from '../../ui/legacy/legacy.js';

import type * as StyleIntelligence from './style_intelligence.js';

const UIStrings = {
  /**
   * @description Title of the Style Intelligence tool in the Elements sidebar.
   */
  styleIntelligence: 'Intelligence',
  /**
   * @description Command for showing Style Intelligence in the Elements sidebar.
   */
  showStyleIntelligence: 'Show Style Intelligence',
} as const;

const str_ = i18n.i18n.registerUIStrings('panels/style_intelligence/style_intelligence-meta.ts', UIStrings);
const i18nLazyString = i18n.i18n.getLazilyComputedLocalizedString.bind(undefined, str_);

let loadedModule: typeof StyleIntelligence|undefined;

async function loadModule(): Promise<typeof StyleIntelligence> {
  if (!loadedModule) {
    loadedModule = await import('./style_intelligence.js');
  }
  return loadedModule;
}

UI.ViewManager.registerViewExtension({
  location: UI.ViewManager.ViewLocationValues.ELEMENTS_SIDEBAR,
  id: 'elements.style-intelligence',
  commandPrompt: i18nLazyString(UIStrings.showStyleIntelligence),
  title: i18nLazyString(UIStrings.styleIntelligence),
  order: 2,
  persistence: UI.ViewManager.ViewPersistence.PERMANENT,
  async loadView() {
    const module = await loadModule();
    return module.StyleIntelligencePane.StyleIntelligencePane.instance();
  },
});
