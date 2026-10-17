// Live verification of the Style Studio preview engine (Checkpoint 2A) against
// a built Avora.
//
// Serves style-studio-preview.html over HTTP, launches Avora with DevTools
// docked, drives the real Elements selection and Studio targeting, applies
// previews through the pane's engine, and checks results with getComputedStyle
// in the page itself, never only a protocol response. It ends with the DevTools
// close experiment: Studio's preview released at close, a simulated orphan left
// by a crashed session, and a second DevTools session in which every sheet
// keeps its own text and its id, and nothing sweeps the orphan.
//
// Usage (from the repo root, after `npm run build`):
//   chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
//     docs/style-intelligence/fixtures/verify-style-studio-preview.mjs <out-dir>
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const {default: puppeteer} = await import(path.join(
    ROOT, 'chromium/src/third_party/devtools-frontend/src/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const binary = path.join(ROOT, 'chromium/src/out/Default/Avora.app/Contents/MacOS/Avora');
const outDir = path.resolve(process.argv[2] ?? 'style-studio-preview-out');
fs.mkdirSync(outDir, {recursive: true});

const results = [];
function check(name, ok, detail = '') {
  results.push({name, ok, detail});
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  — ${detail}` : ''}`);
}
function note(text) {
  results.push({name: text, ok: true, note: true});
  console.log(`NOTE  ${text}`);
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

const server = http.createServer((request, response) => {
  const name = path.basename(new URL(request.url, 'http://x').pathname);
  const file = path.join(HERE, name);
  if (!name.endsWith('.html') || !fs.existsSync(file)) {
    response.writeHead(404).end();
    return;
  }
  response.writeHead(200, {'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store'});
  fs.createReadStream(file).pipe(response);
});
await new Promise(resolve => server.listen(0, 'localhost', resolve));
const fixtureUrl = `http://localhost:${server.address().port}/style-studio-preview.html`;
const DEBUG_PORT = 9000 + Math.floor(Math.random() * 900);

const browser = await puppeteer.launch({
  executablePath: binary,
  headless: false,
  devtools: true,
  defaultViewport: null,
  userDataDir: path.join(outDir, 'profile'),
  // The second DevTools session connects over remote debugging from its own origin.
  args: ['--no-first-run', '--no-default-browser-check', '--window-size=1500,1000', `--remote-debugging-port=${DEBUG_PORT}`,
         `--remote-allow-origins=http://localhost:${DEBUG_PORT}`],
});

/** Installs helpers in a DevTools frontend page. */
async function installHelpers(devtools) {
  await devtools.evaluate(async () => {
    const UI = await import('./ui/legacy/legacy.js');
    const SDK = await import('./core/sdk/sdk.js');
    const Common = await import('./core/common/common.js');
    await UI.ViewManager.ViewManager.instance().showView('elements');
    await UI.ViewManager.ViewManager.instance().showView('elements.style-intelligence');
    const SI = await import('./panels/style_intelligence/style_intelligence.js');
    const pane = () => SI.StyleIntelligencePane.StyleIntelligencePane.instance();
    const v = {UI, SDK, Common, SI, pane};
    v.domModel = () => SDK.TargetManager.TargetManager.instance().primaryPageTarget().model(SDK.DOMModel.DOMModel);
    v.cssModel = () => SDK.TargetManager.TargetManager.instance().primaryPageTarget().model(SDK.CSSModel.CSSModel);
    v.settle = () => new Promise(r => setTimeout(r, 500));
    v.select = async selector => {
      const domModel = v.domModel();
      const doc = await domModel.requestDocument();
      const nodeId = await domModel.querySelector(doc.id, selector);
      await Common.Revealer.reveal(domModel.nodeForId(nodeId));
      await v.settle();
      return nodeId;
    };
    v.studio = () => pane().contentElement.querySelector('.studio');
    v.waitFor = async (predicate, timeout = 5000) => {
      const start = Date.now();
      while (!predicate()) {
        if (Date.now() - start > timeout) {
          throw new Error('timed out waiting');
        }
        await new Promise(r => setTimeout(r, 50));
      }
    };
    v.openStudio = async () => {
      await v.waitFor(() => pane().contentElement.querySelector('#tab-studio'));
      pane().contentElement.querySelector('#tab-studio').click();
      await v.waitFor(() => v.studio());
    };
    v.radio = async (group, index) => {
      v.studio().querySelectorAll(`input[name="${group}"]`)[index].click();
      await v.settle();
    };
    v.type = async text => {
      const input = v.studio().querySelector('.studio-selector');
      input.value = text;
      input.dispatchEvent(new Event('input', {bubbles: true}));
      input.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
      await v.settle();
    };
    v.preview = () => pane().studioPreview();
    v.ref = () => v.preview().currentRef();
    v.apply = async css => {
      const ref = v.ref();
      if (!ref) {
        return {ok: false, reason: 'no-ref', state: v.preview().engine.state().status};
      }
      const result = await v.preview().engine.apply(ref, css);
      return {ok: result.ok, reason: result.reason ?? null, detail: result.detail ?? null, state: result.state.status,
              rule: result.state.preview?.ruleText ?? null, invalid: result.state.preview?.declarations.invalid ?? null};
    };
    v.revert = async () => {
      const result = await v.preview().engine.revert();
      return {ok: result.ok, state: result.state.status};
    };
    v.state = () => v.preview().engine.state().status;
    v.effect = async nodeId => await v.preview().engine.inspectEffect(nodeId);
    /** Every inspector stylesheet of the page, with its text. */
    v.inspectorSheets = async () => {
      const cssModel = v.cssModel();
      const sheets = [];
      for (const header of cssModel.styleSheetHeaders()) {
        if (header.isViaInspector()) {
          sheets.push({id: header.id, frameId: header.frameId, text: await cssModel.getStyleSheetText(header.id)});
        }
      }
      return sheets;
    };
    globalThis.__v = v;
  });
}

const errors = [];
try {
  const [page] = await browser.pages();
  await page.goto(fixtureUrl);
  const devtoolsTarget = await browser.waitForTarget(t => t.url().startsWith('devtools://'), {timeout: 30000});
  const devtools = await devtoolsTarget.asPage();
  devtools.on('pageerror', e => errors.push(e.message));
  devtools.on('console', m => {
    if (m.type() === 'error') {
      errors.push(m.text());
    }
  });
  await devtools.waitForFunction(() => document.readyState === 'complete', {timeout: 30000});
  await sleep(3000);
  await installHelpers(devtools);
  const step = (fn, arg) => devtools.evaluate(fn, arg);
  const computed = (selector, property) => page.evaluate(
      (s, p) => getComputedStyle(document.querySelector(s)).getPropertyValue(p).trim(), selector, property);
  const computedIn = (selector, property) => page.evaluate((s, p) => {
    const frame = document.getElementById('pv-frame');
    return getComputedStyle(frame.contentDocument.querySelector(s)).getPropertyValue(p).trim();
  }, selector, property);
  const resetMutations = () => page.evaluate(() => {
    window.__mutations = [];
  });
  const mutations = () => page.evaluate(() => window.__mutations);

  await step(async () => {
    await globalThis.__v.openStudio();
  });
  await resetMutations();

  // --- Ownership: Studio's own forced sheet; the Styles pane's default is separate. ---
  let r = await step(async () => {
    const v = globalThis.__v;
    const before = (await v.inspectorSheets()).length;
    await v.select('#pv-a');
    await v.radio('studio-source', 0);
    const applied = await v.apply('color: rgb(101, 0, 0)');
    const sheetsAfterApply = await v.inspectorSheets();
    const owned = v.preview().engine.ownedSheetIds();
    const doc = await v.domModel().requestDocument();
    const node = v.domModel().nodeForId(await v.domModel().querySelector(doc.id, '#pv-a'));
    const header = v.cssModel().styleSheetHeaderForId(owned[0]);
    // Exactly what "New Style Rule" in the Styles pane and the view-transition helper call.
    const shared = await v.cssModel().requestViaInspectorStylesheet(node.frameId());
    return {
      before, applied, owned, shared: shared?.id ?? null,
      afterApply: sheetsAfterApply.map(sheet => sheet.id),
      after: (await v.inspectorSheets()).map(sheet => sheet.id),
      header: {
        viaInspector: header?.isViaInspector() ?? null,
        frameMatches: header?.frameId === node.frameId(),
        ownerNode: header?.ownerNode?.backendNodeId() ?? null,
        documentBackendNodeId: doc.backendNodeId(),
      },
      knownDefault: {
        shared: shared ? v.cssModel().isKnownDefaultInspectorStylesheet(shared.id) : null,
        studio: v.cssModel().isKnownDefaultInspectorStylesheet(owned[0]),
      },
    };
  });
  check('Studio creates only its own forced sheet: no default inspector stylesheet is created for it',
        r.applied.ok && r.before === 0 && r.afterApply.length === 1 && r.owned.length === 1 && r.afterApply[0] === r.owned[0],
        JSON.stringify({before: r.before, afterApply: r.afterApply, owned: r.owned}));
  check('Studio\'s sheet header is an inspector sheet of the target frame, owned by the target document',
        r.header.viaInspector === true && r.header.frameMatches && r.header.ownerNode !== null &&
            r.header.ownerNode === r.header.documentBackendNodeId,
        JSON.stringify(r.header));
  check('the Styles pane gets a separate default inspector stylesheet, never Studio\'s',
        r.shared !== null && !r.owned.includes(r.shared) && r.after.length === 2 && r.knownDefault.shared === true &&
            r.knownDefault.studio === false,
        `shared=${r.shared} owned=${r.owned} ${JSON.stringify(r.knownDefault)}`);
  const studioSheet = r.owned[0];
  const sharedSheet = r.shared;

  // --- Single element: only the target changes. ---
  r = {
    a: await computed('#pv-a', 'color'),
    b: await computed('#pv-b', 'color'),
    c: await computed('#pv-c', 'color'),
  };
  check('a single-element preview changes only that element', r.a === 'rgb(101, 0, 0)' && r.b === 'rgb(10, 10, 10)' &&
        r.c === 'rgb(10, 10, 10)', JSON.stringify(r));

  // A user rule added the way the Styles pane adds one survives every Studio operation.
  r = await step(async (sharedId) => {
    const v = globalThis.__v;
    const cssModel = v.cssModel();
    const text = await cssModel.getStyleSheetText(sharedId) ?? '';
    // The same write ElementsPanel makes to the shared inspector stylesheet.
    const rule = await cssModel.setStyleSheetText(sharedId, `${text}\n#pv-c { outline-color: rgb(0, 0, 77); }`, false) === null;
    await v.apply('color: rgb(102, 0, 0)');
    await v.revert();
    await v.apply('color: rgb(103, 0, 0)');
    return {added: Boolean(rule), after: await cssModel.getStyleSheetText(sharedId)};
  }, sharedSheet);
  check('a rule added through the Styles pane path survives Studio apply, revert, and apply',
        r.added && r.after.includes('outline-color: rgb(0, 0, 77)') && !r.after.includes('avora-style-studio'),
        JSON.stringify(r.after));
  check('the user\'s rule is in effect on the page', await computed('#pv-c', 'outline-color') === 'rgb(0, 0, 77)');

  // --- Replace, repeated replacement, revert. ---
  r = await step(async () => {
    const v = globalThis.__v;
    await v.apply('color: rgb(104, 0, 0)');
    const replaced = await v.apply('background-color: rgb(0, 104, 0)');
    for (let i = 0; i < 5; i++) {
      await v.apply(`background-color: rgb(0, ${110 + i}, 0)`);
    }
    return {replaced, sheets: await v.inspectorSheets(), owned: v.preview().engine.ownedSheetIds()};
  });
  const studioTexts = r.sheets.filter(s => s.text?.startsWith('/* avora-style-studio-preview'));
  check('replacements reuse one sheet and keep only the latest rule', studioTexts.length === 1 && r.owned.length === 1 &&
        (studioTexts[0].text.match(/\{/g) ?? []).length === 1 && studioTexts[0].text.includes('rgb(0, 114, 0)'),
        JSON.stringify(studioTexts.map(s => s.text)));
  r = {color: await computed('#pv-a', 'color'), background: await computed('#pv-a', 'background-color')};
  check('a replacement removes the previous declarations entirely', r.color === 'rgb(10, 10, 10)' &&
        r.background === 'rgb(0, 114, 0)', JSON.stringify(r));
  r = await step(async () => await globalThis.__v.revert());
  const afterRevert = {color: await computed('#pv-a', 'color'), background: await computed('#pv-a', 'background-color')};
  check('revert restores the original computed style', r.ok && r.state === 'none' && afterRevert.color === 'rgb(10, 10, 10)' &&
        afterRevert.background === 'rgba(0, 0, 0, 0)', JSON.stringify(afterRevert));

  // --- Invalid CSS. ---
  r = await step(async () => {
    const v = globalThis.__v;
    const bad = await v.apply('color: rgb(1, 2, 3) } body { display: none');
    const partial = await v.apply('color: rgb(105, 0, 0); colr: blue');
    return {bad, partial};
  });
  check('CSS that would escape the rule is rejected and nothing changes', !r.bad.ok && r.bad.reason === 'invalid-css' &&
        await computed('body', 'display') === 'block', JSON.stringify(r.bad));
  check('invalid declarations are reported and the valid ones applied', r.partial.ok &&
        JSON.stringify(r.partial.invalid) === '["colr: blue"]' && await computed('#pv-a', 'color') === 'rgb(105, 0, 0)',
        JSON.stringify(r.partial));
  await step(async () => await globalThis.__v.revert());

  // --- All matches. ---
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-b');
    await v.radio('studio-source', 1);
    await v.type('.pv-item');
    await v.radio('studio-scope', 1);
    return await v.apply('color: rgb(106, 0, 0)');
  });
  const all = await page.evaluate(() => [...document.querySelectorAll('.pv-item')].map(e => getComputedStyle(e).color));
  check('an all-matches preview changes every match', r.ok && all.every(c => c === 'rgb(106, 0, 0)'), JSON.stringify(all));
  await step(async () => {
    const v = globalThis.__v;
    await v.revert();
    await v.radio('studio-scope', 0);
  });
  r = await step(async () => await globalThis.__v.apply('color: rgb(107, 0, 0)'));
  const single = await page.evaluate(() => [...document.querySelectorAll('#pv-list .pv-item')].map(e => getComputedStyle(e).color));
  check('the same selector in one-element scope changes only the selected element',
        r.ok && JSON.stringify(single) === JSON.stringify(['rgb(10, 10, 10)', 'rgb(107, 0, 0)', 'rgb(10, 10, 10)']),
        `${r.rule} · ${JSON.stringify(single)}`);
  await step(async () => {
    const v = globalThis.__v;
    await v.revert();
    await v.radio('studio-source', 0);
  });

  // --- Cascade conflicts, with the measured effect. ---
  const cascade = async (selector, css, property) => {
    return await step(async ([sel, declarations]) => {
      const v = globalThis.__v;
      const nodeId = await v.select(sel);
      await v.radio('studio-source', 1);
      await v.type('.pv-box');
      const applied = await v.apply(declarations);
      const effect = await v.effect(nodeId);
      return {applied, effect, ref: v.ref()};
    }, [selector, css]).then(async result => ({...result, computed: await computed(selector, property)}));
  };
  r = await cascade('#pv-plain', 'color: rgb(108, 0, 0)', 'color');
  check('same specificity as a page class rule: the preview wins, and the effect says so',
        r.computed === 'rgb(108, 0, 0)' && r.effect?.[0]?.state === 'active', JSON.stringify(r.effect));
  r = await cascade('#pv-id', 'color: rgb(109, 0, 0)', 'color');
  check('a more specific page id rule wins over a class-specific preview, and the effect says overridden',
        r.computed === 'rgb(12, 12, 12)' && r.effect?.[0]?.state === 'overridden', JSON.stringify(r.effect));
  r = await cascade('#pv-inline', 'color: rgb(110, 0, 0)', 'color');
  check('an inline style wins over a preview', r.computed === 'rgb(16, 16, 16)' && r.effect?.[0]?.state === 'overridden',
        JSON.stringify(r.effect));
  r = await cascade('#pv-imp', 'color: rgb(111, 0, 0)', 'color');
  check('a page !important wins: no !important is added for the author', r.computed === 'rgb(13, 13, 13)' &&
        !r.applied.rule.slice(r.applied.rule.indexOf('{')).includes('important') &&
            r.effect?.[0]?.state === 'overridden',
        r.applied.rule);
  r = await cascade('#pv-imp', 'color: rgb(112, 0, 0) !important', 'color');
  check('an author !important keeps its meaning', r.computed === 'rgb(112, 0, 0)', r.applied.rule);
  r = await cascade('#pv-layered', 'color: rgb(113, 0, 0)', 'color');
  check('an unlayered preview wins over a layered page rule, even a more specific one', r.computed === 'rgb(113, 0, 0)',
        JSON.stringify(r.effect));
  await step(async () => await globalThis.__v.revert());
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-parent');
    await v.radio('studio-source', 0);
    return await v.apply('color: rgb(114, 0, 0)');
  });
  check('an inherited property reaches descendants', r.ok && await computed('#pv-child', 'color') === 'rgb(114, 0, 0)');
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-token');
    return await v.apply('--pv-gap: 12px');
  });
  check('a custom property flows into the declarations that use it', r.ok &&
        await computed('#pv-token', 'padding-left') === '12px');
  await step(async () => await globalThis.__v.revert());
  check('cascade previews leave every element as it was', await computed('#pv-plain', 'color') === 'rgb(11, 11, 11)' &&
        await computed('#pv-token', 'padding-left') === '4px' && await computed('#pv-child', 'color') === 'rgb(15, 15, 15)');

  // --- No DOM mutation, no change to authored stylesheets. ---
  r = await page.evaluate(() => ({
    mutations: window.__mutations,
    authoredSame: window.__authored() === window.__authoredBefore,
    sheets: document.styleSheets.length,
    sheetsBefore: window.__sheetCount,
  }));
  check('previews never mutated the page DOM', r.mutations.length === 0, JSON.stringify(r.mutations));
  check('the page\'s authored stylesheets are unchanged and Studio\'s sheet is not in document.styleSheets',
        r.authoredSame && r.sheets === r.sheetsBefore, `sheets ${r.sheetsBefore} → ${r.sheets}`);

  // --- External modification of Studio's sheet. ---
  r = await step(async (sheetId) => {
    const v = globalThis.__v;
    await v.select('#pv-a');
    await v.apply('color: rgb(115, 0, 0)');
    const cssModel = v.cssModel();
    const text = await cssModel.getStyleSheetText(sheetId);
    await cssModel.setStyleSheetText(sheetId, `${text}\n#pv-b { color: rgb(0, 0, 115); }`, true);
    await v.settle();
    const state = v.state();
    const blocked = await v.apply('color: rgb(116, 0, 0)');
    const after = await cssModel.getStyleSheetText(sheetId);
    const reverted = await v.revert();
    return {state, blocked, kept: after.includes('rgb(0, 0, 115)'), reverted, final: await cssModel.getStyleSheetText(sheetId)};
  }, studioSheet);
  check('an external change to Studio\'s sheet is detected and not written over', r.state === 'conflict' &&
        !r.blocked.ok && r.blocked.reason === 'external-modification' && r.kept, JSON.stringify(r.blocked));
  check('revert then empties Studio\'s own sheet', r.reverted.ok && r.final.trim() === '/* avora-style-studio-preview v1 */',
        JSON.stringify(r.final));

  // --- Position shift: the element moved into the old position is never styled. ---
  await resetMutations();
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-shift-target');
    return await v.apply('color: rgb(117, 0, 0)');
  });
  await page.evaluate(() => window.__actions.shift());
  await sleep(800);
  r = {
    target: await computed('#pv-shift-target', 'color'),
    inserted: await computed('#pv-shift-new', 'color'),
    rule: await step(() => globalThis.__v.preview().engine.state().preview?.ruleText ?? null),
  };
  check('after a sibling is inserted before the target, only the target keeps the preview',
        r.target === 'rgb(117, 0, 0)' && r.inserted === 'rgb(10, 10, 10)', JSON.stringify(r));
  await step(async () => await globalThis.__v.revert());

  // --- Element removal. ---
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-remove');
    return await v.apply('color: rgb(118, 0, 0)');
  });
  await page.evaluate(() => window.__actions.remove());
  await sleep(1000);
  r = await step(async (sheetId) => ({
    state: globalThis.__v.state(),
    text: await globalThis.__v.cssModel().getStyleSheetText(sheetId),
  }), studioSheet);
  check('removing the previewed element reverts the preview', r.state === 'none' && !r.text.includes('{'),
        JSON.stringify(r));

  // --- Single-element revalidation. The preview stays on exactly the element it
  // was applied to, or is reverted; it never moves to another element. ---
  const PAGE = 'rgb(30, 30, 30)';
  const mColors = () => page.evaluate(() => Object.fromEntries(
      [...document.querySelectorAll('#pv-mut > *, #pv-mut-dest > *')].map(e => [e.id, getComputedStyle(e).color])));
  /** Selects `target`, types `selector` in single-element scope, and applies `css`. */
  const applyTo = (target, selector, css, scope = 0) => step(async ([t, sel, declarations, sc]) => {
    const v = globalThis.__v;
    await v.select(t);
    await v.radio('studio-source', 1);
    await v.type(sel);
    await v.radio('studio-scope', sc);
    return await v.apply(declarations);
  }, [target, selector, css, scope]);
  const engineState = () => step(() => ({
    state: globalThis.__v.state(),
    rule: globalThis.__v.preview().engine.state().preview?.ruleText ?? null,
  }));
  const styledOnly = (colors, color, ids) =>
      Object.entries(colors).every(([id, c]) => ids.includes(id) ? c === color : c === PAGE);

  r = await applyTo('#pv-m2', '.pv-m', 'color: rgb(130, 0, 0)');
  check('a single-element preview on one of several matches styles only that element',
        r.ok && styledOnly(await mColors(), 'rgb(130, 0, 0)', ['pv-m2']), JSON.stringify(await mColors()));
  await page.evaluate(() => window.__actions.removeM1());
  await sleep(800);
  r = {colors: await mColors(), ...await engineState()};
  check('removing an earlier sibling keeps the preview on the same element, rewritten for its new position',
        r.state === 'applied' && r.rule.includes(':nth-child(1)') && styledOnly(r.colors, 'rgb(130, 0, 0)', ['pv-m2']),
        JSON.stringify(r));
  await page.evaluate(() => window.__actions.appendM('pv-m5'));
  await sleep(800);
  r = {colors: await mColors(), ...await engineState()};
  check('a new element matching the selector is not styled by a single-element preview',
        r.state === 'applied' && styledOnly(r.colors, 'rgb(130, 0, 0)', ['pv-m2']), JSON.stringify(r));
  await page.evaluate(() => window.__actions.moveM2());
  await sleep(1000);
  r = {colors: await mColors(), ...await engineState()};
  // Blink's DOM agent unbinds a removed node, so a moved element comes back
  // under a new node id: Studio cannot prove it is the same element and reverts.
  check('moving the element elsewhere never styles another element (the preview follows it or is reverted)',
        styledOnly(r.colors, 'rgb(130, 0, 0)', r.state === 'applied' ? ['pv-m2'] : []), JSON.stringify(r));
  note(`after the element moved, the preview was ${r.state === 'applied' ? 'kept on it' : 'reverted'}`);
  await step(async () => await globalThis.__v.revert());

  r = await applyTo('#pv-m3', '.pv-m', 'color: rgb(131, 0, 0)');
  await page.evaluate(() => window.__actions.replace('pv-m3'));
  await sleep(1000);
  r = {applied: r.ok, colors: await mColors(), ...await engineState()};
  check('replacing the element with an identical copy reverts; the copy is not styled',
        r.applied && r.state === 'none' && styledOnly(r.colors, '', []), JSON.stringify(r));

  r = await applyTo('#pv-m4', '.pv-m', 'color: rgb(132, 0, 0)');
  await page.evaluate(() => window.__actions.setClass('pv-m4', 'pv-other'));
  await sleep(1000);
  r = {applied: r.ok, m4: await computed('#pv-m4', 'color'), colors: await mColors(), ...await engineState()};
  check('a class change that stops the selector matching reverts the preview',
        r.applied && r.state === 'none' && !Object.values(r.colors).includes('rgb(132, 0, 0)'), JSON.stringify(r));
  await page.evaluate(() => window.__actions.setClass('pv-m4', 'pv-m'));

  r = await applyTo('#pv-m4', '#pv-m4', 'color: rgb(133, 0, 0)');
  await page.evaluate(() => window.__actions.setId('pv-m4', 'pv-m4-renamed'));
  await sleep(1000);
  r = {applied: r.ok, colors: await mColors(), ...await engineState()};
  check('an id change that stops the selector matching reverts the preview',
        r.applied && r.state === 'none' && !Object.values(r.colors).includes('rgb(133, 0, 0)'), JSON.stringify(r));
  await page.evaluate(() => window.__actions.setId('pv-m4-renamed', 'pv-m4'));
  await sleep(500);

  // All matches is the selector itself: CSS semantics, by design.
  r = await applyTo('#pv-m4', '.pv-m', 'color: rgb(134, 0, 0)', 1);
  const allBefore = await mColors();
  await page.evaluate(() => window.__actions.appendM('pv-m6'));
  await page.evaluate(() => window.__actions.setClass('pv-m5', 'pv-other'));
  await sleep(1000);
  r = {applied: r.ok, before: allBefore, colors: await mColors(), ...await engineState()};
  check('an all-matches preview styles every current match, including new ones, and none that stop matching',
        r.applied && r.state === 'applied' && Object.values(r.before).every(c => c === 'rgb(134, 0, 0)') &&
            r.colors['pv-m6'] === 'rgb(134, 0, 0)' && r.colors['pv-m5'] === 'rgb(0, 0, 0)',
        JSON.stringify(r));
  await step(async () => {
    const v = globalThis.__v;
    await v.revert();
    await v.radio('studio-scope', 0);
    await v.radio('studio-source', 0);
  });
  check('after revert every element is back to its page style',
        Object.entries(await mColors()).every(([id, c]) => c === (id === 'pv-m5' ? 'rgb(0, 0, 0)' : PAGE)),
        JSON.stringify(await mColors()));

  // --- Transient targeting: between a DOM change and Studio's re-check. ---
  // Read synchronously after each mutation, in the page, before any protocol
  // event can reach DevTools; then timed frame by frame until it changes.
  const transient = (body, color, selector) => page.evaluate(
      async ([b, c, s]) => await window.__actions.transient(new Function(b), c, s), [body, color, selector]);
  const GAP = '#pv-gap > *';
  r = await applyTo('#pv-g2', '.pv-g', 'color: rgb(140, 0, 0)');
  let t = await transient(`const x = document.createElement('div'); x.className = 'pv-g'; x.id = 'pv-g0';
      document.getElementById('pv-g2').before(x);`, 'rgb(140, 0, 0)', GAP);
  await sleep(800);
  let settled = await page.evaluate(() => [...document.querySelectorAll('#pv-gap > *')]
                                              .filter(e => getComputedStyle(e).color === 'rgb(140, 0, 0)').map(e => e.id));
  check('a sibling inserted before the target: nothing is styled in the gap, then the target is again',
        r.ok && t.atOnce.length === 0 && JSON.stringify(settled) === '["pv-g2"]', JSON.stringify({t, settled}));
  t = await transient(`document.getElementById('pv-g1').remove();`, 'rgb(140, 0, 0)', GAP);
  await sleep(800);
  settled = await page.evaluate(() => [...document.querySelectorAll('#pv-gap > *')]
                                          .filter(e => getComputedStyle(e).color === 'rgb(140, 0, 0)').map(e => e.id));
  check('an earlier sibling removed: nothing is styled in the gap, then the target is again',
        t.atOnce.length === 0 && JSON.stringify(settled) === '["pv-g2"]', JSON.stringify({t, settled}));
  t = await transient(`const p = document.getElementById('pv-gap'); p.insertBefore(document.getElementById('pv-g3'),
      document.getElementById('pv-g2'));`, 'rgb(140, 0, 0)', GAP);
  await sleep(800);
  settled = await page.evaluate(() => [...document.querySelectorAll('#pv-gap > *')]
                                          .filter(e => getComputedStyle(e).color === 'rgb(140, 0, 0)').map(e => e.id));
  check('a different sibling reordered into the target\'s place is not styled in the gap',
        t.atOnce.length === 0 && JSON.stringify(settled) === '["pv-g2"]', JSON.stringify({t, settled}));
  await step(async () => await globalThis.__v.revert());

  // The known limit: an element identical in tag, id, and class at exactly the target's path.
  r = await applyTo('#pv-twins > :first-child', '.pv-twin', 'color: rgb(141, 0, 0)');
  t = await transient(`const p = document.getElementById('pv-twins'); p.append(p.firstElementChild);`,
                      'rgb(141, 0, 0)', '#pv-twins > *');
  await sleep(1000);
  const twins = {applied: r.ok, t, state: (await engineState()).state,
                 after: await page.evaluate(() => [...document.querySelectorAll('#pv-twins > *')]
                                                      .map(e => getComputedStyle(e).color))};
  // The re-check either reverts or keeps the preview on the original twin; never the other one.
  check('identical twins swapped: the other twin is styled only until the re-check',
        twins.applied && (twins.t.after.length === 0 || JSON.stringify(twins.t.after) === '["Twin one"]') &&
            (twins.state === 'none' ? twins.after.every(c => c === 'rgb(40, 40, 40)') :
                                      JSON.stringify(twins.after) === '["rgb(40, 40, 40)","rgb(141, 0, 0)"]'),
        JSON.stringify(twins));
  await step(async () => await globalThis.__v.revert());
  note(`identical twins swapped: styled at once ${JSON.stringify(twins.t.atOnce)}, ` +
       `cleared after ${twins.t.ms} ms (${twins.t.frames} frames)`);
  r = await applyTo('#pv-twins > :first-child', '.pv-twin', 'color: rgb(142, 0, 0)');
  t = await transient(`const e = document.getElementById('pv-twins').firstElementChild; e.replaceWith(e.cloneNode(true));`,
                      'rgb(142, 0, 0)', '#pv-twins > *');
  await sleep(1000);
  const replaced = {applied: r.ok, t, state: (await engineState()).state};
  check('an element replaced by an identical copy: the copy is styled only until the re-check, which reverts',
        replaced.applied && replaced.state === 'none' && replaced.t.after.length === 0, JSON.stringify(replaced));
  note(`identical copy replaced the target: styled at once ${JSON.stringify(replaced.t.atOnce)}, ` +
       `cleared after ${replaced.t.ms} ms (${replaced.t.frames} frames)`);
  await step(async () => {
    const v = globalThis.__v;
    await v.radio('studio-scope', 0);
    await v.radio('studio-source', 0);
  });

  // --- Target change. ---
  await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-a');
    await v.apply('color: rgb(119, 0, 0)');
    await v.select('#pv-b');
  });
  r = {a: await computed('#pv-a', 'color'), b: await computed('#pv-b', 'color'),
       state: await step(() => globalThis.__v.state())};
  check('changing the target reverts the previous target\'s preview', r.a === 'rgb(10, 10, 10)' &&
        r.b === 'rgb(10, 10, 10)' && r.state === 'none', JSON.stringify(r));

  // --- A stale target during application. ---
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-a');
    const ref = v.ref();
    const pending = v.preview().engine.apply(ref, 'color: rgb(120, 0, 0)');
    // The selection moves on while the apply is still checking its target.
    const domModel = v.domModel();
    const doc = await domModel.requestDocument();
    v.Common.Revealer.reveal(domModel.nodeForId(await domModel.querySelector(doc.id, '#pv-c')));
    const result = await pending;
    await v.settle();
    return {ok: result.ok, reason: result.reason ?? null, state: v.state()};
  });
  r = {...r, a: await computed('#pv-a', 'color'), c: await computed('#pv-c', 'color')};
  check('an apply overtaken by a selection change leaves no preview on either element',
        r.state === 'none' && r.a === 'rgb(10, 10, 10)' && r.c === 'rgb(10, 10, 10)', JSON.stringify(r));

  // --- Frame destruction. ---
  r = await step(async () => {
    const v = globalThis.__v;
    const domModel = v.domModel();
    const doc = await domModel.requestDocument();
    const owner = domModel.nodeForId(await domModel.querySelector(doc.id, '#pv-frame'));
    await owner.getChildNodesPromise();
    const frameDoc = owner.contentDocument();
    const nodeId = await domModel.querySelector(frameDoc.id, '#pv-frame-p');
    await v.Common.Revealer.reveal(domModel.nodeForId(nodeId));
    await v.settle();
    return await v.apply('color: rgb(121, 0, 0)');
  });
  const framed = await computedIn('#pv-frame-p', 'color');
  check('a preview in a same-origin iframe applies there', r.ok && framed === 'rgb(121, 0, 0)', `${r.rule} · ${framed}`);
  await page.evaluate(() => window.__actions.removeFrame());
  await sleep(1000);
  r = await step(() => ({state: globalThis.__v.state(), owned: globalThis.__v.preview().engine.ownedSheetIds()}));
  check('destroying the frame drops its preview and its sheet', r.state === 'none', JSON.stringify(r));

  // --- Navigation during application. ---
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-a');
    return v.ref();
  });
  const raceRef = r;
  const racing = step(async (ref) => {
    const result = await globalThis.__v.preview().engine.apply(ref, 'color: rgb(122, 0, 0)');
    return {ok: result.ok, reason: result.reason ?? null};
  }, raceRef);
  await page.reload();
  const raceResult = await racing.catch(e => ({ok: false, reason: `threw: ${e.message}`}));
  await sleep(1500);
  r = {raceResult, color: await computed('#pv-a', 'color'), state: await step(() => globalThis.__v.state())};
  check('an apply racing a reload leaves the new page unstyled and no tracked preview',
        r.color === 'rgb(10, 10, 10)' && r.state === 'none', JSON.stringify(r));
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-a');
    return await v.apply('color: rgb(123, 0, 0)');
  });
  check('after a reload a new preview gets a new sheet in the new document', r.ok &&
        await computed('#pv-a', 'color') === 'rgb(123, 0, 0)', JSON.stringify(r));

  // --- Pane disposal. ---
  r = await step(async () => {
    const v = globalThis.__v;
    const old = v.pane();
    v.SI.StyleIntelligencePane.StyleIntelligencePane.instance({forceNew: true});
    await new Promise(res => setTimeout(res, 800));
    return {oldState: old.studioPreview().engine.state().status};
  });
  check('disposing the pane reverts its preview', r.oldState === 'none' && await computed('#pv-a', 'color') === 'rgb(10, 10, 10)',
        JSON.stringify(r));

  // --- DevTools close experiment. ---
  // A fresh pane instance is not attached to the UI; use its engine directly.
  r = await step(async () => {
    const v = globalThis.__v;
    const fresh = v.SI.StyleIntelligencePane.StyleIntelligencePane.instance();
    const domModel = v.domModel();
    const doc = await domModel.requestDocument();
    const node = domModel.nodeForId(await domModel.querySelector(doc.id, '#pv-a'));
    const ref = v.SI.StyleStudioDom.previewRefForNode(node, 'node', '#pv-a');
    const result = await fresh.studioPreview().engine.apply(ref, 'color: rgb(124, 0, 0)');
    return {ok: result.ok, reason: result.reason ?? null};
  });
  check('a preview is applied before closing DevTools', r.ok && await computed('#pv-a', 'color') === 'rgb(124, 0, 0)',
        JSON.stringify(r));
  // A marked sheet no engine owns: what a crashed DevTools session would leave.
  r = await step(async () => {
    const v = globalThis.__v;
    const cssModel = v.cssModel();
    const frameId = v.SDK.TargetManager.TargetManager.instance().primaryPageTarget().model(
        v.SDK.ResourceTreeModel.ResourceTreeModel).mainFrame.id;
    const header = await cssModel.createInspectorStylesheet(frameId, true);
    const error = await cssModel.setStyleSheetText(
        header.id, '/* avora-style-studio-preview v1 */\n#pv-b { color: rgb(125, 0, 0); }\n', true);
    return {ok: error === null, id: header.id};
  });
  const orphanId = r.id;
  check('a simulated orphan from a crashed session is in effect', r.ok && await computed('#pv-b', 'color') === 'rgb(125, 0, 0)');
  // A user rule in the inspector stylesheet the Styles pane uses, made the way
  // "New Style Rule" makes one, after Studio has written its own sheet.
  r = await step(async () => {
    const v = globalThis.__v;
    const cssModel = v.cssModel();
    const frameId = v.SDK.TargetManager.TargetManager.instance().primaryPageTarget().model(
        v.SDK.ResourceTreeModel.ResourceTreeModel).mainFrame.id;
    const shared = await cssModel.requestViaInspectorStylesheet(frameId);
    const owned = new Set(v.pane().studioPreview().engine.ownedSheetIds());
    const text = await cssModel.getStyleSheetText(shared.id) ?? '';
    const error = await cssModel.setStyleSheetText(shared.id, `${text}\n#pv-c { outline-color: rgb(0, 0, 77); }`, false);
    return {ok: error === null, sharedIsStudios: owned.has(shared.id)};
  });
  check('a Styles-pane rule is added before closing, and not into Studio\'s sheet',
        r.ok && !r.sharedIsStudios && await computed('#pv-c', 'outline-color') === 'rgb(0, 0, 77)', JSON.stringify(r));

  const beforeCloseSheets = await step(async () => (await globalThis.__v.inspectorSheets()).map(
      sheet => `${sheet.id}: ${JSON.stringify(sheet.text)}`));
  const studioIdsBeforeClose = await step(() => globalThis.__v.SI.StyleIntelligencePane.StyleIntelligencePane.instance()
                                                    .studioPreview().engine.ownedSheetIds());
  // What a ledger would record: the browser's id for the page, from Target.getTargetInfo.
  // (A frontend attached straight to the page's websocket names its target "main".)
  const targetIdBeforeClose = await step(async () => (await globalThis.__v.SDK.TargetManager.TargetManager.instance()
                                                          .primaryPageTarget().targetAgent().invoke_getTargetInfo({}))
                                                         .targetInfo?.targetId ?? null);
  console.log('DIAG session 1 inspector sheets before close:', JSON.stringify(beforeCloseSheets));
  await devtools.close();
  await sleep(1500);
  const afterClose = {owned: await computed('#pv-a', 'color'), orphan: await computed('#pv-b', 'color')};
  note(`after DevTools closed: Studio's own preview on #pv-a is ${afterClose.owned} ` +
       `(${afterClose.owned === 'rgb(10, 10, 10)' ? 'released at close' : 'still applied'}); ` +
       `the simulated orphan on #pv-b is ${afterClose.orphan}`);

  // A second DevTools session for the same page. Nothing in it identifies or
  // empties the orphan.
  const pageTargetId = page.target()._targetId;
  const frontend = await browser.newPage();
  const frontendErrors = [];
  frontend.on('pageerror', e => frontendErrors.push(e.message));
  frontend.on('console', m => {
    if (m.type() === 'error') {
      frontendErrors.push(m.text());
    }
  });
  await frontend.goto(
      `http://localhost:${DEBUG_PORT}/devtools/devtools_app.html?ws=localhost:${DEBUG_PORT}/devtools/page/${pageTargetId}`);
  await sleep(6000);
  const afterReopen = {owned: await computed('#pv-a', 'color'), orphan: await computed('#pv-b', 'color')};
  const reopenedState = await frontend.evaluate(async () => {
    const SDK = await import('./core/sdk/sdk.js');
    const target = SDK.TargetManager.TargetManager.instance().primaryPageTarget();
    const cssModel = target?.model(SDK.CSSModel.CSSModel);
    // Headers arrive as CSS.enable reports them; wait until the inspector ones are in.
    for (let i = 0; i < 100 && !(cssModel?.styleSheetHeaders() ?? []).some(h => h.isViaInspector()); i++) {
      await new Promise(r => setTimeout(r, 100));
    }
    await new Promise(r => setTimeout(r, 500));
    const sheets = [];
    for (const header of cssModel?.styleSheetHeaders() ?? []) {
      if (header.isViaInspector()) {
        sheets.push(`${header.id}: ${JSON.stringify(await cssModel.getStyleSheetText(header.id))}`);
      }
    }
    const domModel = target?.model(SDK.DOMModel.DOMModel);
    const doc = await domModel.requestDocument();
    const nodeId = await domModel.querySelector(doc.id, '#pv-b');
    const matched = await cssModel.getMatchedStyles(nodeId);
    const rules = matched ? matched.nodeStyles().map(style => `${style.styleSheetId ?? 'inline'} ${style.parentRule?.origin ?? ''} ${style.cssText ?? ''}`) : [];
    const targetId = (await target?.targetAgent().invoke_getTargetInfo({}))?.targetInfo?.targetId ?? null;
    return {url: target?.inspectedURL() ?? null, targetId, enabled: cssModel?.isEnabled() ?? null,
            sheets, rules,
            allHeaders: cssModel.styleSheetHeaders().map(h => `${h.id}:${h.origin}:${h.frameId}`)};
  }).catch(e => ({error: e.message}));
  // With per-sheet text storage (Checkpoint 2A.1) each sheet reports its own text.
  const reopenedTexts = (reopenedState.sheets ?? []).map(entry => entry.slice(entry.indexOf(': ') + 2));
  // Exactly one sheet holds the user's rule and exactly one the orphan; every
  // other one is an emptied Studio sheet. Before the fix all of them reported
  // the same text.
  const holding = needle => reopenedTexts.filter(t => t.includes(needle)).length;
  check('after reopening, each inspector sheet reports its own text',
        holding('pv-c') === 1 && holding('rgb(125, 0, 0)') === 1 &&
            reopenedTexts.filter(t => !t.includes('pv-c') && !t.includes('rgb(125, 0, 0)'))
                .every(t => t === '"/* avora-style-studio-preview v1 */\\n"'),
        JSON.stringify(reopenedState.sheets));
  // "New Style Rule" in the reopened session, through the real frontend path.
  r = await frontend.evaluate(async () => {
    const SDK = await import('./core/sdk/sdk.js');
    const target = SDK.TargetManager.TargetManager.instance().primaryPageTarget();
    const cssModel = target.model(SDK.CSSModel.CSSModel);
    const frameId = target.model(SDK.ResourceTreeModel.ResourceTreeModel).mainFrame.id;
    const shared = await cssModel.requestViaInspectorStylesheet(frameId);
    const text = await cssModel.getStyleSheetText(shared.id) ?? '';
    const error = await cssModel.setStyleSheetText(shared.id, `${text}\n#pv-plain { outline-color: rgb(0, 88, 0); }`, false);
    return {ok: error === null, wroteInto: text};
  }).catch(e => ({ok: false, error: e.message}));
  check('after reopening, a new Styles-pane rule keeps the earlier user rule',
        r.ok && await computed('#pv-c', 'outline-color') === 'rgb(0, 0, 77)' &&
            await computed('#pv-plain', 'outline-color') === 'rgb(0, 88, 0)',
        `${JSON.stringify(r)} · #pv-c ${await computed('#pv-c', 'outline-color')}`);
  // Stylesheet ids come from a per-renderer map keyed by the sheet object
  // (IdentifiersFactory::IdForCSSStyleSheet), so a later session sees the same
  // ids. That is what a persisted ledger of Studio's sheet ids would rely on.
  const reopenedIds = (reopenedState.allHeaders ?? []).map(entry => entry.split(':')[0]);
  check('a reopened session reports Studio\'s and the orphan\'s sheets under the same ids',
        studioIdsBeforeClose.length === 1 && reopenedIds.includes(studioIdsBeforeClose[0]) && reopenedIds.includes(orphanId),
        JSON.stringify({studioIdsBeforeClose, orphanId, reopenedIds}));
  check('the docked and the reopened frontend both get the browser\'s page target id from Target.getTargetInfo',
        targetIdBeforeClose === pageTargetId && reopenedState.targetId === pageTargetId,
        JSON.stringify({targetIdBeforeClose, reopened: reopenedState.targetId, pageTargetId}));
  // Crash cleanup is a separate question: nothing identifies an orphan's owner.
  note(`the unreleased orphan is still applied after reopening (${afterReopen.orphan}); ` +
       'orphan cleanup remains a separate limitation');
  check('Studio\'s own preview was released when DevTools closed', afterClose.owned === 'rgb(10, 10, 10)', afterClose.owned);
  r = await page.evaluate(() => ({authoredSame: window.__authored() === window.__authoredBefore}));
  check('the page\'s authored stylesheets are still unchanged at the end', r.authoredSame);
} finally {
  server.close();
  fs.writeFileSync(path.join(outDir, 'results.json'), JSON.stringify(results, null, 2));
  await browser.close();
}
console.log('DevTools frontend errors:', JSON.stringify(errors.filter(e => /style_intelligence|StyleStudio/i.test(e))));
const failed = results.filter(r => !r.ok).length;
console.log(`\n${results.filter(r => !r.note).length - failed}/${results.filter(r => !r.note).length} checks passed`);
process.exit(failed ? 1 : 0);
