// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chai';

import {renderElementIntoDOM} from '../../testing/DOMHelpers.js';
import {setupLocaleHooks} from '../../testing/LocaleHelpers.js';
import {render} from '../../ui/lit/lit.js';

import * as StyleIntelligence from './style_intelligence.js';

type StudioPreviewInput = StyleIntelligence.StyleStudioView.StudioPreviewInput;
type StudioViewInput = StyleIntelligence.StyleStudioView.StudioViewInput;
type StudioViewOutput = StyleIntelligence.StyleStudioView.StudioViewOutput;
const {canSubmitPreview, previewStatus, renderStudioPanel} = StyleIntelligence.StyleStudioView;

function preview(overrides: Partial<StudioPreviewInput> = {}): StudioPreviewInput {
  return {
    canApply: true,
    draft: '',
    applied: null,
    conflict: false,
    dirty: false,
    busy: null,
    event: {kind: 'none'},
    effects: null,
    ...overrides,
  };
}

const APPLIED = {declarations: 'color: red;', invalid: [], scope: 'node' as const};
const active = {name: 'color', value: 'red', state: 'active' as const, computed: 'rgb(255, 0, 0)'};
const overridden = {name: 'margin', value: '4px', state: 'overridden' as const, computed: '0px'};

describe('StyleStudioView preview status', () => {
  setupLocaleHooks();

  it('says nothing is applied, and when a draft is not applied yet', () => {
    assert.strictEqual(previewStatus(preview()).tone, 'idle');
    assert.include(previewStatus(preview()).text, 'No preview applied');
    const draft = previewStatus(preview({draft: 'color: red', dirty: true}));
    assert.strictEqual(draft.tone, 'pending');
    assert.include(draft.text, 'not applied');
  });

  it('describes an applied preview by its measured effect, not by insertion', () => {
    assert.include(previewStatus(preview({applied: APPLIED, event: {kind: 'applied'}, effects: [active]})).text,
                   'Every declaration takes effect');
    const partly = previewStatus(preview({applied: APPLIED, event: {kind: 'applied'}, effects: [active, overridden]}));
    assert.strictEqual(partly.tone, 'warning');
    assert.include(partly.text, '1 of 2 declarations are overridden by the cascade');
    assert.include(previewStatus(preview({applied: APPLIED, event: {kind: 'applied'}})).text, 'could not be measured');
  });

  it('distinguishes an update from a first apply', () => {
    assert.include(previewStatus(preview({applied: APPLIED, event: {kind: 'updated'}, effects: [active]})).text,
                   'Preview updated');
  });

  it('says when the draft differs from what the page shows', () => {
    const status = previewStatus(preview({applied: APPLIED, draft: 'color: blue', dirty: true, effects: [active]}));
    assert.include(status.text, 'Draft changes not applied');
  });

  it('reports revert, invalid CSS, failures, and removals by cause', () => {
    assert.include(previewStatus(preview({event: {kind: 'reverted'}})).text, 'reverted');
    assert.include(previewStatus(preview({event: {kind: 'invalid'}})).text, 'your draft is kept');
    assert.include(previewStatus(preview({event: {kind: 'failed', reason: 'target-detached'}})).text,
                   'no longer in the page');
    assert.include(previewStatus(preview({event: {kind: 'failed', reason: 'record-failed'}})).text,
                   'could not save the record');
    assert.include(previewStatus(preview({event: {kind: 'target-changed'}})).text, 'target or scope changed');
    assert.include(previewStatus(preview({event: {kind: 'target-detached'}})).text, 'left the page');
    assert.include(previewStatus(preview({event: {kind: 'navigated'}})).text, 'navigated');
    assert.include(previewStatus(preview({event: {kind: 'revert-failed'}})).text, 'could not be removed');
    assert.include(previewStatus(preview({applied: APPLIED, conflict: true})).text, 'changed elsewhere');
  });

  it('shows progress while busy', () => {
    assert.include(previewStatus(preview({busy: 'apply'})).text, 'Applying');
    assert.include(previewStatus(preview({busy: 'revert'})).text, 'Reverting');
  });

  it('allows a submit only with a target, a draft, nothing in progress, and something new to apply', () => {
    assert.isTrue(canSubmitPreview(preview({draft: 'color: red', dirty: true})));
    assert.isFalse(canSubmitPreview(preview({draft: '  '})));
    assert.isFalse(canSubmitPreview(preview({draft: 'color: red', canApply: false})));
    assert.isFalse(canSubmitPreview(preview({draft: 'color: red', busy: 'apply'})));
    assert.isFalse(canSubmitPreview(preview({draft: 'color: red', applied: APPLIED, dirty: false})));
    assert.isTrue(canSubmitPreview(preview({draft: 'color: blue', applied: APPLIED, dirty: true})));
    assert.isFalse(canSubmitPreview(preview({draft: 'color: blue', applied: APPLIED, dirty: true, conflict: true})));
  });
});

describe('StyleStudioView preview controls', () => {
  setupLocaleHooks();

  let container: HTMLElement;
  let calls: string[];

  const output: StudioViewOutput = {
    setSource: () => {},
    setScope: () => {},
    setSelectorText: () => {},
    commitSelector: () => {},
    highlightTarget: () => {},
    hideHighlight: () => {},
    togglePick: () => {},
    setDraft: text => calls.push(`draft ${text}`),
    applyPreview: () => calls.push('apply'),
    revertPreview: () => calls.push('revert'),
    removeRecovered: id => calls.push(`remove ${id}`),
    keepRecovered: id => calls.push(`keep ${id}`),
  };

  function show(previewInput: StudioPreviewInput, recovery: StudioViewInput['recovery'] = {
    items: [],
    busy: false,
    result: null,
  }): void {
    const input: StudioViewInput = {
      nodeLabel: 'div#a',
      notice: null,
      canPick: true,
      picking: false,
      source: 'selected-node',
      scope: 'node',
      selectorText: '',
      target: {kind: 'node', nodeId: 1, label: 'div#a', documentKey: 't:1', frameId: 'F', selector: null, matchCount: null},
      preview: previewInput,
      recovery,
    };
    render(renderStudioPanel(input, output), container);
  }

  beforeEach(() => {
    calls = [];
    container = document.createElement('div');
    renderElementIntoDOM(container);
  });

  afterEach(() => {
    container.remove();
  });

  const textarea = (): HTMLTextAreaElement => container.querySelector('textarea.studio-css') as HTMLTextAreaElement;
  const button = (cls: string): HTMLElement&{disabled: boolean} =>
      container.querySelector(`devtools-button.${cls}`) as HTMLElement & {disabled: boolean};

  it('labels the editor and keeps its status in a live region', () => {
    show(preview({draft: 'color: red', dirty: true}));
    const label = container.querySelector('label[for="studio-css"]');
    assert.strictEqual(label?.textContent?.trim(), 'CSS declarations');
    assert.strictEqual(textarea().value, 'color: red');
    const status = container.querySelector('.studio-preview-status');
    assert.strictEqual(status?.getAttribute('role'), 'status');
    assert.strictEqual(status?.getAttribute('aria-live'), 'polite');
  });

  it('reports draft edits without applying them', () => {
    show(preview());
    textarea().value = 'color: green';
    textarea().dispatchEvent(new Event('input'));
    assert.deepEqual(calls, ['draft color: green']);
  });

  it('applies with the button or Ctrl+Enter only when a submit is allowed', () => {
    show(preview({draft: 'color: red', dirty: true}));
    assert.isFalse(button('studio-apply').disabled);
    button('studio-apply').click();
    textarea().dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', ctrlKey: true}));
    assert.deepEqual(calls, ['apply', 'apply']);

    calls = [];
    show(preview({draft: 'color: red', dirty: true, busy: 'apply'}));
    assert.isTrue(button('studio-apply').disabled);
    button('studio-apply').click();
    textarea().dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', metaKey: true}));
    assert.deepEqual(calls, []);
  });

  it('names the button Update and shows what is applied once a preview is on the page', () => {
    show(preview({draft: 'color: blue', dirty: true, applied: APPLIED, effects: [active, overridden]}));
    assert.include(button('studio-apply').textContent, 'Update preview');
    assert.isFalse(button('studio-revert').disabled);
    assert.include(container.querySelector('.studio-applied-css')?.textContent, 'color: red;');
    const items = [...container.querySelectorAll('.studio-effects li')].map(li => li.className);
    assert.deepEqual(items, ['active', 'overridden']);
  });

  it('disables Revert when nothing is applied', () => {
    show(preview({draft: 'color: red', dirty: true}));
    assert.isTrue(button('studio-revert').disabled);
  });

  it('offers Remove and Keep for an earlier session\'s preview, and Forget for a changed sheet', () => {
    show(preview(), {
      items: [{id: 's1', kind: 'orphan', css: '#a { color: red; }'}, {id: 's2', kind: 'changed', css: ''}],
      busy: false,
      result: null,
    });
    assert.include(container.querySelector('.studio-recovery-item.orphan')?.textContent, 'earlier DevTools session');
    button('studio-recovery-remove').click();
    button('studio-recovery-keep').click();
    button('studio-recovery-forget').click();
    assert.deepEqual(calls, ['remove s1', 'keep s1', 'keep s2']);
  });
});
