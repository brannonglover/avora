// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Style Studio's reversible CSS preview engine.
 *
 * A preview is one rule in an inspector stylesheet that Studio created and owns.
 * The page's own stylesheets, its DOM, and every other inspector stylesheet are
 * never written. Applying or replacing a preview rewrites the owned sheet as a
 * whole, so previews never accumulate; reverting empties it.
 *
 * The rule's selector carries the specificity of the selector the CSS is
 * authored for, and nothing is made `!important` that the author did not mark,
 * so the preview takes part in the normal cascade. Whether it then wins is a
 * separate, measured question (see PreviewHost.inspectEffect).
 */

/**
 * First line of every sheet Studio writes, so a person reading the sheet in the
 * Styles pane can tell what it is. It is a label, not an identity: nothing
 * decides ownership from sheet text (see `PreviewHost.verifyOwnership`).
 */
export const PREVIEW_MARKER = '/* avora-style-studio-preview v1 */';

export type PreviewScope = 'node'|'all-matches';

/** What a preview is for. Built from a Style Studio target at the moment of use. */
export interface PreviewTargetRef {
  /** DevTools target id: node ids and sheets are only meaningful within one target. */
  targetId: string;
  frameId: string;
  /** Target id plus document node id; changes on navigation. */
  documentKey: string;
  scope: PreviewScope;
  /** The one node a `node` preview may affect. */
  nodeId: number|null;
  /** The selector the CSS is authored for. It sets the rule's specificity. */
  selector: string;
}

export type PreviewFailure =
    /** The CSS is not a list of declarations, or contained no valid declaration. */
    'invalid-css'|
    /** The target left the page, or its document did. */
    'target-detached'|
    /** The target can't be previewed (shadow tree, pseudo-element, non-element). */
    'target-unsupported'|
    /** The authoring selector is invalid or does not match the target. */
    'selector-mismatch'|
    /** The single-element rule would not match exactly the target. */
    'not-unique'|
    /** The sheet failed Studio's ownership checks, so it was not written. */
    'ownership-unsafe'|
    /** Studio's sheet no longer holds what Studio last wrote. */
    'external-modification'|
    /** The protocol rejected the write, or the sheet did not read back as written. */
    'write-failed'|
    /** A newer operation or a target change made this one obsolete before it wrote. */
    'superseded'|'disposed';

/** Declarations after the browser parsed them, and the input it rejected. */
export interface ParsedDeclarations {
  /** The browser's own serialization; the only declaration text ever written. */
  text: string;
  /** Input declarations the browser dropped as invalid. */
  invalid: string[];
}

export interface AppliedPreview {
  ref: PreviewTargetRef;
  sheetId: string;
  /** The complete rule written, selector included. */
  ruleText: string;
  declarations: ParsedDeclarations;
}

export type PreviewState = {
  status: 'none',
}|{
  status: 'applied',
  preview: AppliedPreview,
}|{
  /**
   * Studio's sheet was changed by something else. Studio will not write over it;
   * only a revert, which empties Studio's own sheet, leaves this state.
   */
  status: 'conflict',
  preview: AppliedPreview,
};

export type PreviewResult = {
  ok: true,
  state: PreviewState,
}|{
  ok: false,
  reason: PreviewFailure,
  detail: string,
  /** Always a state Studio fully tracks: the last good preview, or none. */
  state: PreviewState,
};

export type RuleSelectorResult = {
  ok: true,
  ruleSelector: string,
}|{
  ok: false,
  reason: 'target-detached'|'target-unsupported'|'selector-mismatch'|'not-unique',
  detail: string,
};

/** Effect of one preview declaration on one element, measured from the cascade. */
export interface PreviewPropertyEffect {
  name: string;
  value: string;
  /** Whether the cascade lets this declaration win for the element. */
  state: 'active'|'overridden'|'unknown';
  computed: string|null;
}

export type CreateSheetFailure = 'ownership-unsafe'|'write-failed'|'target-detached';

/** The DevTools operations the engine needs, for one target's document. */
export interface PreviewHost {
  /**
   * Checks the target now and returns the selector the rule must use: the
   * authoring selector for `all-matches`; for `node`, a selector verified to
   * match exactly that node and nothing else.
   */
  resolveRuleSelector(ref: PreviewTargetRef): Promise<RuleSelectorResult>;
  /**
   * Creates a new forced inspector stylesheet for Studio alone, in `ref`'s
   * document, and checks it with `verifyOwnership` before returning it. Never
   * creates or touches the document's default inspector stylesheet.
   */
  createOwnedSheet(ref: PreviewTargetRef): Promise<{ok: true, sheetId: string}|
                                                   {ok: false, reason: CreateSheetFailure, detail: string}>;
  /** Whether the sheet still exists in its document. */
  sheetExists(sheetId: string): boolean;
  /**
   * Checks what DevTools knows about a sheet the engine created: an inspector
   * stylesheet of `ref`'s frame, owned by `ref`'s live document, and not a
   * default inspector stylesheet. Returns why not, or null. The engine calls
   * it before every write; it never decides by sheet text or by the order
   * sheets were reported in.
   */
  verifyOwnership(ref: PreviewTargetRef, sheetId: string): string|null;
  getText(sheetId: string): Promise<string|null>;
  setText(sheetId: string, text: string): Promise<boolean>;
  /**
   * Sends a write at once, without waiting for any reply. Used only while
   * DevTools is closing, when nothing asynchronous is certain to run.
   */
  setTextNow(sheetId: string, text: string): void;
  /** Reads, from the cascade, how the preview's declarations fare on `nodeId`. */
  inspectEffect(nodeId: number, sheetId: string): Promise<PreviewPropertyEffect[]|null>;
}

/** Splits declaration text on top-level semicolons, respecting strings, parens, and brackets. */
export function splitDeclarations(css: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let quote: string|null = null;
  let start = 0;
  for (let i = 0; i < css.length; i++) {
    const char = css[i];
    if (quote) {
      if (char === '\\') {
        i++;
      } else if (char === quote) {
        quote = null;
      }
      continue;
    }
    if (char === '"' || char === '\'') {
      quote = char;
    } else if (char === '(' || char === '[') {
      depth++;
    } else if ((char === ')' || char === ']') && depth > 0) {
      depth--;
    } else if (char === ';' && depth === 0) {
      parts.push(css.slice(start, i));
      start = i + 1;
    }
  }
  parts.push(css.slice(start));
  return parts.map(part => part.trim()).filter(Boolean);
}

/**
 * Parses `css` as the body of a single rule, with the browser's own parser.
 * Anything that would leave the rule (a `}`, another rule, a nested rule, an
 * at-rule) makes the whole input invalid; declarations the browser drops are
 * reported. The result is the browser's serialization, never the raw input.
 */
export function parseDeclarations(css: string): ParsedDeclarations|null {
  const body = css.trim();
  if (!body) {
    return null;
  }
  const rule = parseSingleRule(`x{${body}}`);
  if (!rule || rule.selectorText !== 'x' || !rule.style.length) {
    return null;
  }
  const invalid = splitDeclarations(body).filter(part => {
    const single = parseSingleRule(`x{${part}}`);
    return !single || !single.style.length;
  });
  return {text: rule.style.cssText, invalid};
}

/** The rule as written, checked to parse back as exactly one plain style rule. */
export function buildPreviewRule(ruleSelector: string, declarations: ParsedDeclarations): string|null {
  const text = `${ruleSelector} { ${declarations.text} }`;
  const rule = parseSingleRule(text);
  if (!rule) {
    return null;
  }
  return text;
}

/** The whole text of an owned sheet holding one preview rule. */
export function previewSheetText(ruleText: string): string {
  return `${PREVIEW_MARKER}\n${ruleText}\n`;
}

/** One element on the path from `:root` down to a single-element preview's target. */
export interface PathStep {
  localName: string;
  /** 1-based position among its parent's element children. */
  position: number;
  /** How many element children its parent has. */
  siblings: number;
}

/** The target's own identifying attributes, matched exactly; null when absent. */
export interface TargetIdentity {
  id: string|null;
  className: string|null;
}

/** A CSS string literal holding exactly `value`. */
function cssString(value: string): string {
  return `"${value.replace(/[\\"]/g, '\\$&').replace(/\n/g, '\\a ').replace(/\r/g, '\\d ').replace(/\f/g, '\\c ').replace(/\0/g, '\\fffd ')}"`;
}

/** An identifier escaped for a selector, as `CSS.escape` does. */
function cssIdent(value: string): string {
  return CSS.escape(value);
}

/**
 * The selector for exactly one element, matched by its path from `:root`.
 *
 * Every step names its tag and its position counted from both ends of its
 * parent (`:nth-child(i):nth-last-child(j)`), so inserting or removing an
 * element sibling anywhere on the path makes the rule match nothing rather
 * than the element that took the old position. The target step also matches
 * the target's `id` and `class` exactly. The rule can still match another
 * element only if a same-count reorder or a replacement puts an element with
 * the same tag at every step, and the same `id` and `class`, at exactly the
 * same path; the engine's re-check after every DOM change then reverts it.
 *
 * `:where()` gives the path no specificity, and `:is()` gives the rule the
 * authoring selector's, so the preview cascades like the CSS will once
 * authored. The element must also match the authoring selector.
 */
export function singleElementSelector(
    path: readonly PathStep[], identity: TargetIdentity, authoringSelector: string): string {
  const steps = path.map(
      step => `${cssIdent(step.localName)}:nth-child(${step.position}):nth-last-child(${step.siblings - step.position + 1})`);
  const own = (identity.id === null ? ':not([id])' : `[id=${cssString(identity.id)}]`) +
      (identity.className === null ? ':not([class])' : `[class=${cssString(identity.className)}]`);
  const parts = [':root', ...steps];
  parts[parts.length - 1] += own;
  return `:where(${parts.join(' > ')}):is(${authoringSelector})`;
}

function parseSingleRule(text: string): CSSStyleRule|null {
  const sheet = new CSSStyleSheet();
  try {
    sheet.replaceSync(text);
  } catch {
    return null;
  }
  if (sheet.cssRules.length !== 1) {
    return null;
  }
  const rule = sheet.cssRules[0];
  if (!(rule instanceof CSSStyleRule) || rule.cssRules.length) {
    return null;
  }
  return rule;
}

/** Recorded as an owned sheet's text when a failed write left it unknown; never equal to real text. */
const UNKNOWN_TEXT = '\u0000unknown';

/** What an owned sheet holds with no preview in it. */
const EMPTY_TEXT = `${PREVIEW_MARKER}\n`;

interface OwnedSheet {
  ref: PreviewTargetRef;
  sheetId: string;
  /** Exactly what Studio last wrote, so any other change is detectable. */
  written: string;
}

function documentScope(ref: PreviewTargetRef): string {
  return `${ref.targetId}|${ref.frameId}|${ref.documentKey}`;
}

function sameRef(a: PreviewTargetRef, b: PreviewTargetRef): boolean {
  return a.targetId === b.targetId && a.frameId === b.frameId && a.documentKey === b.documentKey &&
      a.scope === b.scope && a.nodeId === b.nodeId && a.selector === b.selector;
}

/**
 * Applies, replaces, and reverts one preview at a time.
 *
 * Ownership: the engine writes only sheets it created itself in this session
 * (`#sheets`), and checks each with `PreviewHost.verifyOwnership` immediately
 * before every write, revert included. A sheet that fails is never written and
 * is no longer tracked.
 *
 * Ordering: operations run one after another. Each captures the epoch when it
 * is requested; `invalidate`, `targetChanged`, and `dispose` advance it. An
 * operation re-resolves its target immediately before writing, gives up if the
 * epoch moved before it wrote, and reverts its own write if the epoch moved
 * while the write was in flight. On any failure the engine is left with the
 * last good preview or none, never with a sheet whose contents it doesn't know
 * unless that sheet is held in conflict, to be emptied by the next revert.
 */
export class StylePreviewEngine {
  readonly #hostFor: (ref: PreviewTargetRef) => PreviewHost | null;
  /** The sheets this engine created, one per target, frame, and document, reused across previews. */
  readonly #sheets = new Map<string, OwnedSheet>();
  #state: PreviewState = {status: 'none'};
  #queue: Promise<unknown> = Promise.resolve();
  /** Advanced by `invalidate` and `dispose`; an operation from an older epoch never writes. */
  #epoch = 0;
  #disposed = false;
  readonly #onChange: (state: PreviewState) => void;

  constructor(hostFor: (ref: PreviewTargetRef) => PreviewHost | null, onChange: (state: PreviewState) => void = () => {}) {
    this.#hostFor = hostFor;
    this.#onChange = onChange;
  }

  state(): PreviewState {
    return this.#state;
  }

  /** The preview for `ref`, if that is what is applied. */
  stateFor(ref: PreviewTargetRef): PreviewState {
    const state = this.#state;
    return state.status !== 'none' && sameRef(state.preview.ref, ref) ? state : {status: 'none'};
  }

  /** Applies `css` to `ref`, replacing any current preview whole. */
  apply(ref: PreviewTargetRef, css: string): Promise<PreviewResult> {
    const epoch = this.#epoch;
    return this.#enqueue(() => this.#apply(ref, css, epoch));
  }

  /** Same as apply: a preview is always replaced whole, never appended to. */
  replace(ref: PreviewTargetRef, css: string): Promise<PreviewResult> {
    return this.apply(ref, css);
  }

  /** Removes the current preview by emptying Studio's own sheet. */
  revert(): Promise<PreviewResult> {
    return this.#enqueue(() => this.#revert());
  }

  /**
   * The target changed, left the page, or Studio is going away: operations
   * requested before this never leave a preview behind, and the current
   * preview is reverted.
   */
  invalidate(): Promise<PreviewResult> {
    this.#epoch++;
    return this.revert();
  }

  /**
   * An owned sheet changed. If it no longer holds what Studio wrote while a
   * preview is applied, the preview is in conflict and is not written over.
   */
  sheetChanged(sheetId: string): Promise<void> {
    return this.#enqueue(async () => {
      const state = this.#state;
      if (state.status === 'applied' && state.preview.sheetId === sheetId) {
        const host = this.#hostFor(state.preview.ref);
        const sheet = this.#sheets.get(documentScope(state.preview.ref));
        if (host && sheet && sheet.sheetId === sheetId && host.sheetExists(sheetId) &&
            await host.getText(sheetId) !== sheet.written && this.#state === state) {
          this.#setState({status: 'conflict', preview: state.preview});
        }
      }
      return {ok: true, state: this.#state};
    }).then(() => undefined);
  }

  /**
   * Re-checks the applied preview's target after a DOM change. The target is
   * the same element (`nodeId`) throughout: if it moved, or siblings came or
   * went, the single-element rule is rewritten for its new position; if it can
   * no longer be resolved exactly (gone, replaced, no longer matching the
   * authoring selector), the preview is reverted. It never moves to another
   * element.
   */
  revalidate(): Promise<PreviewResult> {
    const epoch = this.#epoch;
    return this.#enqueue(async () => {
      const state = this.#state;
      if (state.status !== 'applied') {
        return {ok: true, state};
      }
      const host = this.#hostFor(state.preview.ref);
      const resolved = host ? await host.resolveRuleSelector(state.preview.ref) : null;
      if (resolved?.ok) {
        const ruleText = buildPreviewRule(resolved.ruleSelector, state.preview.declarations);
        if (ruleText === state.preview.ruleText) {
          return {ok: true, state: this.#state};
        }
      }
      if (this.#state !== state) {
        return {ok: true, state: this.#state};
      }
      if (!resolved?.ok) {
        const reverted = await this.#revert();
        return reverted.ok ? this.#fail(resolved?.reason ?? 'target-detached', resolved?.detail ?? '') : reverted;
      }
      return await this.#apply(state.preview.ref, state.preview.declarations.text, epoch);
    });
  }

  /** A sheet disappeared with its document; forget it without writing. */
  sheetRemoved(sheetId: string): void {
    for (const [key, sheet] of this.#sheets) {
      if (sheet.sheetId === sheetId) {
        this.#sheets.delete(key);
      }
    }
    if (this.#state.status !== 'none' && this.#state.preview.sheetId === sheetId) {
      this.#setState({status: 'none'});
    }
  }

  /** Measures how the current preview's declarations fare on `nodeId`. */
  async inspectEffect(nodeId: number): Promise<PreviewPropertyEffect[]|null> {
    const state = this.#state;
    if (state.status !== 'applied') {
      return null;
    }
    const host = this.#hostFor(state.preview.ref);
    return host ? await host.inspectEffect(nodeId, state.preview.sheetId) : null;
  }

  /**
   * Best effort while DevTools is closing: sends an emptying write for every
   * owned sheet that may hold a preview, synchronously, and stops. Delivery is
   * not guaranteed. A sheet left holding a preview stays in its document until
   * the document goes away; no later session identifies or empties it.
   */
  releaseNow(): void {
    this.#epoch++;
    this.#disposed = true;
    for (const sheet of this.#sheets.values()) {
      const host = this.#hostFor(sheet.ref);
      if (sheet.written !== EMPTY_TEXT && sheet.written !== '' && host && host.sheetExists(sheet.sheetId) &&
          host.verifyOwnership(sheet.ref, sheet.sheetId) === null) {
        host.setTextNow(sheet.sheetId, EMPTY_TEXT);
      }
    }
    this.#sheets.clear();
    this.#state = {status: 'none'};
  }

  /** Reverts and stops. Any later apply, including one already queued, fails with `disposed`. */
  dispose(): Promise<PreviewResult> {
    this.#epoch++;
    this.#disposed = true;
    return this.revert();
  }

  /**
   * The current Studio target changed. Any operation started for an earlier
   * target can no longer leave a preview, whether or not one is applied yet.
   */
  targetChanged(): Promise<PreviewResult> {
    return this.invalidate();
  }

  ownedSheetIds(): string[] {
    return [...this.#sheets.values()].map(sheet => sheet.sheetId);
  }

  #enqueue(operation: () => Promise<PreviewResult>): Promise<PreviewResult> {
    const run = this.#queue.then(operation, operation);
    this.#queue = run.catch(() => undefined);
    return run;
  }

  #fail(reason: PreviewFailure, detail: string): PreviewResult {
    return {ok: false, reason, detail, state: this.#state};
  }

  #setState(state: PreviewState): void {
    this.#state = state;
    this.#onChange(state);
  }

  /** The sheet is still one this engine created and tracks. */
  #tracked(sheet: OwnedSheet): boolean {
    return this.#sheets.get(documentScope(sheet.ref)) === sheet;
  }

  /** Stops tracking a sheet that failed an ownership check. It is never written again. */
  #drop(sheet: OwnedSheet): void {
    if (this.#tracked(sheet)) {
      this.#sheets.delete(documentScope(sheet.ref));
    }
    if (this.#state.status !== 'none' && this.#state.preview.sheetId === sheet.sheetId) {
      this.#setState({status: 'none'});
    }
  }

  async #apply(ref: PreviewTargetRef, css: string, epoch: number): Promise<PreviewResult> {
    if (this.#disposed) {
      return this.#fail('disposed', 'The preview engine was disposed.');
    }
    if (epoch !== this.#epoch) {
      return this.#fail('superseded', 'The target changed before the preview was written.');
    }
    if (this.#state.status === 'conflict') {
      return this.#fail('external-modification', 'Studio\'s stylesheet was changed elsewhere. Revert first.');
    }
    const declarations = parseDeclarations(css);
    if (!declarations) {
      return this.#fail('invalid-css', 'The CSS must be one or more valid declarations, such as "color: red".');
    }
    const host = this.#hostFor(ref);
    if (!host) {
      return this.#fail('target-detached', 'The target\'s page is no longer available.');
    }
    const resolved = await host.resolveRuleSelector(ref);
    if (!resolved.ok) {
      return this.#fail(resolved.reason, resolved.detail);
    }
    const ruleText = buildPreviewRule(resolved.ruleSelector, declarations);
    if (!ruleText) {
      return this.#fail('invalid-css', 'The preview rule did not parse as a single rule.');
    }
    if (this.#disposed || epoch !== this.#epoch) {
      return this.#fail(this.#disposed ? 'disposed' : 'superseded', 'The target changed before the preview was written.');
    }

    let sheet = await this.#ownedSheet(host, ref);
    if ('reason' in sheet) {
      return this.#fail(sheet.reason, sheet.detail);
    }
    // Write only over exactly what Studio last wrote.
    if (await host.getText(sheet.sheetId) !== sheet.written) {
      const state = this.#state;
      if (state.status !== 'none' && state.preview.sheetId === sheet.sheetId) {
        return this.#conflict(sheet);
      }
      // Changed while it held no preview: leave whatever is there alone, stop
      // tracking it, and use a new sheet.
      this.#drop(sheet);
      sheet = await this.#ownedSheet(host, ref);
      if ('reason' in sheet) {
        return this.#fail(sheet.reason, sheet.detail);
      }
    }

    // Last checks before writing: still current, still Studio's, and the target
    // still resolves to the same rule.
    if (this.#disposed || epoch !== this.#epoch) {
      return this.#fail(this.#disposed ? 'disposed' : 'superseded', 'The target changed before the preview was written.');
    }
    const again = await host.resolveRuleSelector(ref);
    if (!again.ok) {
      return this.#fail(again.reason, again.detail);
    }
    if (again.ruleSelector !== resolved.ruleSelector) {
      return this.#fail('superseded', 'The target moved while the preview was being prepared.');
    }
    if (this.#disposed || epoch !== this.#epoch) {
      return this.#fail(this.#disposed ? 'disposed' : 'superseded', 'The target changed before the preview was written.');
    }
    if (!this.#tracked(sheet) || !host.sheetExists(sheet.sheetId)) {
      return this.#fail('target-detached', 'Studio\'s stylesheet went away with its document.');
    }
    const unsafe = host.verifyOwnership(ref, sheet.sheetId);
    if (unsafe !== null) {
      this.#drop(sheet);
      return this.#fail('ownership-unsafe', unsafe);
    }

    const text = previewSheetText(ruleText);
    const written = await host.setText(sheet.sheetId, text) && await host.getText(sheet.sheetId) === text;
    if (!this.#tracked(sheet) || !host.sheetExists(sheet.sheetId)) {
      // The document, and the sheet with it, went away during the write.
      return this.#fail('target-detached', 'Studio\'s stylesheet went away with its document.');
    }
    if (!written) {
      // Back to the last good text, so the sheet holds nothing untracked.
      const restored = await host.setText(sheet.sheetId, sheet.written) &&
          await host.getText(sheet.sheetId) === sheet.written;
      if (!restored && this.#tracked(sheet)) {
        // Its contents are unknown now. It stays tracked, in conflict, so the
        // next revert or dispose still empties it; nothing writes over it before.
        sheet.written = UNKNOWN_TEXT;
        this.#setState({status: 'conflict', preview: {ref, sheetId: sheet.sheetId, ruleText, declarations}});
      }
      return this.#fail('write-failed', 'The preview could not be written.');
    }
    sheet.written = text;
    sheet.ref = ref;
    const previous = this.#state;
    this.#setState({status: 'applied', preview: {ref, sheetId: sheet.sheetId, ruleText, declarations}});

    // A previous preview in another document's sheet must not stay behind.
    if (previous.status === 'applied' && previous.preview.sheetId !== sheet.sheetId) {
      await this.#emptySheet(previous.preview.sheetId);
    }
    if (this.#disposed || epoch !== this.#epoch) {
      // Superseded while the write was in flight: take it back at once rather
      // than leave an outdated preview until the queued revert runs.
      await this.#revert();
      return this.#fail(this.#disposed ? 'disposed' : 'superseded', 'The target changed while the preview was written.');
    }
    return {ok: true, state: this.#state};
  }

  /** Empties every owned sheet that may hold a preview, the current one first. */
  async #revert(): Promise<PreviewResult> {
    const state = this.#state;
    let ok = true;
    if (state.status !== 'none') {
      ok = await this.#emptySheet(state.preview.sheetId);
    }
    for (const sheet of [...this.#sheets.values()]) {
      if (sheet.written !== EMPTY_TEXT && sheet.written !== '') {
        ok = await this.#emptySheet(sheet.sheetId) && ok;
      }
    }
    if (this.#state.status !== 'none') {
      this.#setState({status: 'none'});
    }
    return ok ? {ok: true, state: this.#state} : this.#fail('write-failed', 'The preview could not be removed.');
  }

  /**
   * Empties one of Studio's own sheets. A sheet that is already gone needs
   * nothing; one that is not tracked, or fails the ownership check, is never
   * written.
   */
  async #emptySheet(sheetId: string): Promise<boolean> {
    const sheet = [...this.#sheets.values()].find(owned => owned.sheetId === sheetId);
    if (!sheet) {
      return true;
    }
    const host = this.#hostFor(sheet.ref);
    if (!host || !host.sheetExists(sheetId)) {
      this.sheetRemoved(sheetId);
      return true;
    }
    if (host.verifyOwnership(sheet.ref, sheetId) !== null) {
      this.#drop(sheet);
      return false;
    }
    const ok = await host.setText(sheetId, EMPTY_TEXT) && await host.getText(sheetId) === EMPTY_TEXT;
    if (ok && this.#tracked(sheet)) {
      sheet.written = EMPTY_TEXT;
    }
    return ok;
  }

  #conflict(sheet: OwnedSheet): PreviewResult {
    const state = this.#state;
    if (state.status === 'applied' && state.preview.sheetId === sheet.sheetId) {
      this.#setState({status: 'conflict', preview: state.preview});
    }
    return this.#fail('external-modification', 'Studio\'s stylesheet was changed elsewhere, so it was not overwritten.');
  }

  /** Reuses this document's owned sheet when it still passes the ownership check, else makes one. */
  async #ownedSheet(host: PreviewHost, ref: PreviewTargetRef):
      Promise<OwnedSheet|{reason: CreateSheetFailure, detail: string}> {
    const key = documentScope(ref);
    const existing = this.#sheets.get(key);
    if (existing && host.sheetExists(existing.sheetId)) {
      const unsafe = host.verifyOwnership(ref, existing.sheetId);
      if (unsafe !== null) {
        this.#drop(existing);
        return {reason: 'ownership-unsafe', detail: unsafe};
      }
      return existing;
    }
    this.#sheets.delete(key);
    const created = await host.createOwnedSheet(ref);
    if (!created.ok) {
      return {reason: created.reason, detail: created.detail};
    }
    if (this.#sheets.has(key)) {
      // Not reachable while operations are serialized; never track two sheets per document.
      return {reason: 'ownership-unsafe', detail: 'Studio already has a stylesheet for this document.'};
    }
    const sheet: OwnedSheet = {ref, sheetId: created.sheetId, written: ''};
    this.#sheets.set(key, sheet);
    return sheet;
  }
}
