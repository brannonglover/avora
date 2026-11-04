// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsManageProfileElement} from './manage_profile.js';

export function getHtml(this: SettingsManageProfileElement) {
  return html`<!--_html_template_start_-->
<settings-subpage page-title="$i18n{editPerson}"
    route-path="${this.routePath}">
<div class="cr-row first manage-profile-section">
  <h1 class="cr-title-text">$i18n{nameYourProfile}</h1>
  <div class="content">
    <cr-input id="nameInput" .value="${this.profileName_}"
        pattern="${this.pattern_}"
        @change="${this.onNameInputChange_}"
        @keydown="${this.onNameInputKeydown_}"
        maxlength="500"
        aria-label="$i18n{nameInputLabel}" auto-validate required
        spellcheck="false" ?disabled="${this.hasEnterpriseLabel_}">
      <cr-icon id="policyIcon" icon="cr:domain" slot="suffix"
          ?hidden="${!this.hasEnterpriseLabel_}">
      </cr-icon>
    </cr-input>
    <cr-tooltip id="tooltip"
    for="policyIcon" position="top">
    $i18n{controlledSettingPolicy}
  </cr-tooltip>
  </div>
</div>
<!-- Avora: no theme colour or avatar here.  A Space carries its own theme,
     made in the Theme Studio (right-click a Space in the sidebar), and the
     account button shows the person's photo or initials. -->
${this.isProfileShortcutSettingVisible_ ? html`
  <div id="outerRow" class="hr">
    <div class="flex" id="labelWrapper">
      <div>$i18n{createShortcutTitle}</div>
      <div class="secondary">$i18n{createShortcutSubtitle}</div>
    </div>
    <cr-toggle id="hasShortcutToggle"
        .checked="${this.hasProfileShortcut_}"
        @change="${this.onHasProfileShortcutChange_}"
        aria-labelledby="labelWrapper">
    </cr-toggle>
  </div>
  <div class="hr"></div>
` : ''}
</settings-subpage>
<!--_html_template_end_-->`;
}
