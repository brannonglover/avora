// Live verification of Style Studio's manual CSS preview UI (Checkpoint 2B)
// against a built Avora, through the real Studio tab: the picker, the CSS
// editor, Apply / Update / Revert, the status line, and crash recovery after
// DevTools is killed. Every visual result is checked with getComputedStyle in
// the page.
//
// Docked DevTools is reopened with Target.openDevTools, so recovery is tested
// against the store docked DevTools really uses: the browser profile's DevTools
// preferences, which live in the browser process.
//
// Usage (from the repo root, after `npm run build`):
//   chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
//     docs/style-intelligence/fixtures/verify-style-studio-editor.mjs <out-dir>
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const {default: puppeteer} = await import(path.join(
    ROOT, 'chromium/src/third_party/devtools-frontend/src/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const outDir = path.resolve(process.argv[2] ?? 'style-studio-editor-out');
fs.mkdirSync(outDir, {recursive: true});

const results = [];
function check(name, ok, detail = '') {
  results.push({name, ok, detail});
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  — ${detail}` : ''}`);
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

const browser = await puppeteer.launch({
  executablePath: path.join(ROOT, 'chromium/src/out/Default/Avora.app/Contents/MacOS/Avora'),
  headless: false,
  devtools: true,
  defaultViewport: null,
  userDataDir: path.join(outDir, 'profile'),
  args: ['--no-first-run', '--no-default-browser-check', '--window-size=1600,1000'],
});

/** Opens the Studio tab in a DevTools frontend page and installs helpers. */
async function setUp(devtools) {
  await devtools.waitForFunction(() => document.readyState === 'complete', {timeout: 30000});
  await sleep(3000);
  await devtools.evaluate(async () => {
    const UI = await import('./ui/legacy/legacy.js');
    const SDK = await import('./core/sdk/sdk.js');
    const Common = await import('./core/common/common.js');
    const Host = await import('./core/host/host.js');
    await UI.ViewManager.ViewManager.instance().showView('elements');
    await UI.ViewManager.ViewManager.instance().showView('elements.style-intelligence');
    const SI = await import('./panels/style_intelligence/style_intelligence.js');
    const v = {UI, SDK, Common, Host, SI};
    v.pane = () => SI.StyleIntelligencePane.StyleIntelligencePane.instance();
    v.root = () => v.pane().contentElement;
    v.settle = (ms = 600) => new Promise(r => setTimeout(r, ms));
    v.waitFor = async (predicate, timeout = 8000) => {
      const start = Date.now();
      while (!predicate()) {
        if (Date.now() - start > timeout) {
          return false;
        }
        await new Promise(r => setTimeout(r, 50));
      }
      return true;
    };
    v.domModel = () => SDK.TargetManager.TargetManager.instance().primaryPageTarget().model(SDK.DOMModel.DOMModel);
    v.cssModel = () => SDK.TargetManager.TargetManager.instance().primaryPageTarget().model(SDK.CSSModel.CSSModel);
    v.select = async selector => {
      const doc = await v.domModel().requestDocument();
      const nodeId = await v.domModel().querySelector(doc.id, selector);
      await Common.Revealer.reveal(v.domModel().nodeForId(nodeId));
      await v.settle();
    };
    v.text = selector => v.root().querySelector(selector)?.textContent.replace(/\s+/g, ' ').trim() ?? null;
    v.previewStatus = () => v.text('.studio-preview-status');
    v.targetStatus = () => v.text('.studio:not(.studio-preview):not(.studio-recovery) .studio-status');
    v.draft = () => v.root().querySelector('textarea.studio-css')?.value ?? null;
    v.button = cls => v.root().querySelector(`devtools-button.${cls}`);
    v.radio = async (group, index) => {
      v.root().querySelectorAll(`input[name="${group}"]`)[index].click();
      await v.settle();
    };
    v.typeSelector = async text => {
      const input = v.root().querySelector('.studio-selector');
      input.value = text;
      input.dispatchEvent(new Event('input', {bubbles: true}));
      input.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
      await v.settle();
    };
    v.state = () => v.pane().studioPreview().engine.state().status;
    v.recoveryItems = () => [...v.root().querySelectorAll('.studio-recovery-item')].map(
        item => ({kind: item.classList.contains('orphan') ? 'orphan' : 'changed', css: item.querySelector('code')?.textContent ?? ''}));
    v.ledgerKeys = () => new Promise(resolve => Host.InspectorFrontendHost.InspectorFrontendHostInstance.getPreferences(
        prefs => resolve(Object.keys(prefs).filter(key => key.startsWith('avora-style-studio-ledger:')))));
    v.ledgerFor = async () => {
      const id = await SI.StyleStudioDom.browserTargetId(SDK.TargetManager.TargetManager.instance().primaryPageTarget());
      return await new Promise(resolve => Host.InspectorFrontendHost.InspectorFrontendHostInstance.getPreference(
          `avora-style-studio-ledger:${id}`, value => resolve(value ? JSON.parse(value) : null)));
    };
    globalThis.__v = v;
    await v.waitFor(() => v.root().querySelector('#tab-studio'), 15000);
    for (let i = 0; i < 20 && !v.root().querySelector('.studio-preview'); i++) {
      v.root().querySelector('#tab-studio')?.click();
      await v.settle(300);
    }
  });
  const ready = await devtools.evaluate(() => Boolean(globalThis.__v.root().querySelector('.studio-preview')));
  if (!ready) {
    throw new Error('The Studio tab did not open');
  }
}

const errors = [];
let devtools;
try {
  const [page] = await browser.pages();
  await page.goto(fixtureUrl);
  devtools = await (await browser.waitForTarget(t => t.url().startsWith('devtools://'), {timeout: 30000})).asPage();
  devtools.on('pageerror', e => errors.push(e.message));
  await setUp(devtools);
  const step = (fn, arg) => devtools.evaluate(fn, arg);
  const color = (selector, property = 'color') => page.evaluate(
      (s, p) => getComputedStyle(document.querySelector(s)).getPropertyValue(p).trim(), selector, property);
  const centerOf = selector => page.evaluate(s => {
    const rect = document.querySelector(s).getBoundingClientRect();
    return {x: rect.left + rect.width / 2, y: rect.top + rect.height / 2};
  }, selector);
  /** Picks an element with Studio's own picker button and a real click on the page. */
  const pick = async selector => {
    const button = await devtools.$('pierce/.studio-pick');
    // The pane may be scrolled down to the editor; bring the picker into view as a user would.
    await button.evaluate(element => element.scrollIntoView({block: 'center'}));
    await sleep(200);
    await button.click();
    await sleep(500);
    const point = await centerOf(selector);
    await page.mouse.move(point.x, point.y);
    await page.mouse.click(point.x, point.y);
    await sleep(1000);
  };
  /** Replaces the draft by typing into the editor with the keyboard. */
  const typeDraft = async text => {
    const textarea = await devtools.$('pierce/textarea.studio-css');
    await textarea.evaluate(element => {
      element.focus();
      element.select();
    });
    await devtools.keyboard.press('Backspace');
    await textarea.type(text);
    await sleep(300);
  };
  const clickButton = async cls => {
    await (await devtools.$(`pierce/devtools-button.${cls}`)).click();
    await sleep(1200);
  };
  const ui = () => step(() => {
    const v = globalThis.__v;
    return {
      status: v.previewStatus(), target: v.targetStatus(), draft: v.draft(), state: v.state(),
      applied: v.text('.studio-applied-css'),
      effects: [...v.root().querySelectorAll('.studio-effects li')].map(li => li.className),
      applyDisabled: v.button('studio-apply')?.disabled ?? null,
      applyLabel: v.text('devtools-button.studio-apply'),
      revertDisabled: v.button('studio-revert')?.disabled ?? null,
    };
  });

  // --- 1. The whole workflow through the UI. ---
  await pick('#pv-a');
  let r = await ui();
  check('picking an element targets it in Studio', /This element only/.test(r.target ?? ''), r.target);
  check('with an empty draft, nothing is applied and Apply is disabled',
        /No preview applied/.test(r.status) && r.applyDisabled === true && r.revertDisabled === true, JSON.stringify(r));
  await typeDraft('color: rgb(150, 0, 0)');
  r = await ui();
  check('typing a draft does not touch the page', await color('#pv-a') === 'rgb(10, 10, 10)' &&
        /Draft not applied/.test(r.status) && r.applyDisabled === false, JSON.stringify(r));
  await clickButton('studio-apply');
  r = await ui();
  check('Apply previews the draft on exactly the picked element',
        await color('#pv-a') === 'rgb(150, 0, 0)' && await color('#pv-b') === 'rgb(10, 10, 10)', JSON.stringify(r));
  check('the status reports the measured effect, and the applied CSS is shown apart from the draft',
        /Every declaration takes effect/.test(r.status) && r.applied === 'color: rgb(150, 0, 0);' &&
            r.effects.join() === 'active' && r.applyDisabled === true && /Update preview/.test(r.applyLabel),
        JSON.stringify(r));
  const ledger = await step(async () => await globalThis.__v.ledgerFor());
  check('the ownership record was persisted with the browser\'s target id, before the preview was visible',
        ledger?.entries?.length === 1 && ledger.entries[0].texts.some(t => t.includes('rgb(150, 0, 0)')) &&
            ledger.targetId === page.target()._targetId,
        JSON.stringify(ledger));
  await typeDraft('color: rgb(151, 0, 0); background-color: rgb(0, 151, 0)');
  r = await ui();
  check('editing the draft leaves the applied preview on the page and says so',
        await color('#pv-a') === 'rgb(150, 0, 0)' && /Draft changes not applied/.test(r.status) &&
            r.applied === 'color: rgb(150, 0, 0);' && r.applyDisabled === false,
        JSON.stringify(r));
  await (await devtools.$('pierce/textarea.studio-css')).focus();
  await devtools.keyboard.down('Control');
  await devtools.keyboard.press('Enter');
  await devtools.keyboard.up('Control');
  await sleep(1200);
  r = await ui();
  check('Ctrl+Enter updates the preview whole', await color('#pv-a') === 'rgb(151, 0, 0)' &&
        await color('#pv-a', 'background-color') === 'rgb(0, 151, 0)' && /Preview updated/.test(r.status),
        JSON.stringify(r));
  await clickButton('studio-revert');
  r = await ui();
  check('Revert restores the original computed styles', await color('#pv-a') === 'rgb(10, 10, 10)' &&
        await color('#pv-a', 'background-color') === 'rgba(0, 0, 0, 0)' && /reverted/.test(r.status) &&
            r.state === 'none' && r.draft.includes('rgb(151, 0, 0)'),
        JSON.stringify(r));
  check('a confirmed revert deletes the ownership record',
        (await step(async () => await globalThis.__v.ledgerFor())) === null);

  // --- 2. Invalid CSS. ---
  await typeDraft('color: red } body { display: none');
  await clickButton('studio-apply');
  r = await ui();
  check('invalid CSS is refused, the page is unchanged, and the draft is kept',
        /Invalid CSS/.test(r.status) && r.state === 'none' && r.draft === 'color: red } body { display: none' &&
            await color('#pv-a') === 'rgb(10, 10, 10)' &&
            await page.evaluate(() => getComputedStyle(document.body).display) === 'block',
        JSON.stringify(r));

  // --- 3. Cascade override. ---
  await pick('#pv-imp');
  await typeDraft('color: rgb(152, 0, 0)');
  await clickButton('studio-apply');
  r = await ui();
  check('a declaration the cascade overrides is reported as overridden, not as applied',
        await color('#pv-imp') === 'rgb(13, 13, 13)' && /1 of 1 declarations are overridden/.test(r.status) &&
            r.effects.join() === 'overridden',
        JSON.stringify(r));
  await clickButton('studio-revert');

  // --- 4. Scope changes never widen a preview silently. ---
  await step(async () => {
    const v = globalThis.__v;
    await v.select('#pv-b');
    await v.radio('studio-source', 1);
    await v.typeSelector('.pv-item');
  });
  await typeDraft('color: rgb(153, 0, 0)');
  await clickButton('studio-apply');
  const itemColors = () => page.evaluate(
      () => [...document.querySelectorAll('#pv-list .pv-item')].map(e => getComputedStyle(e).color));
  r = {...await ui(), colors: await itemColors()};
  check('Selected only with a shared selector styles only the selected element',
        JSON.stringify(r.colors) === '["rgb(10, 10, 10)","rgb(153, 0, 0)","rgb(10, 10, 10)"]', JSON.stringify(r));
  await step(async () => await globalThis.__v.radio('studio-scope', 1));
  await sleep(800);
  r = {...await ui(), colors: await itemColors()};
  check('switching to All matches removes the preview and says why, keeping the draft',
        r.colors.every(c => c === 'rgb(10, 10, 10)') && /target or scope changed/.test(r.status) &&
            r.draft === 'color: rgb(153, 0, 0)' && r.state === 'none',
        JSON.stringify(r));
  await clickButton('studio-apply');
  r = {...await ui(), colors: await itemColors()};
  check('applying again in All matches styles every match', r.colors.every(c => c === 'rgb(153, 0, 0)'),
        JSON.stringify(r));
  await step(async () => {
    const v = globalThis.__v;
    await v.typeSelector('#pv-c');
  });
  await sleep(800);
  r = {...await ui(), colors: await itemColors()};
  check('changing the selector text removes the preview', r.colors.every(c => c === 'rgb(10, 10, 10)') &&
        r.state === 'none' && /target or scope changed/.test(r.status), JSON.stringify(r));
  await step(async () => {
    const v = globalThis.__v;
    await v.radio('studio-scope', 0);
    await v.radio('studio-source', 0);
  });

  // --- 5. Changing the selected element. ---
  await pick('#pv-a');
  await typeDraft('color: rgb(154, 0, 0)');
  await clickButton('studio-apply');
  await pick('#pv-c');
  r = await ui();
  check('selecting another element removes the preview from the old one and says so',
        await color('#pv-a') === 'rgb(10, 10, 10)' && await color('#pv-c') === 'rgb(10, 10, 10)' &&
            /target or scope changed/.test(r.status) && r.draft === 'color: rgb(154, 0, 0)',
        JSON.stringify(r));

  // --- 6. Target removal. ---
  await pick('#pv-remove');
  await clickButton('studio-apply');
  const removedApplied = await color('#pv-remove') === 'rgb(154, 0, 0)';
  await page.evaluate(() => window.__actions.remove());
  await sleep(1200);
  r = await ui();
  check('removing the previewed element removes the preview and explains it',
        removedApplied && r.state === 'none' && /left the page|target or scope changed/.test(r.status),
        JSON.stringify(r));

  // --- 7. Stale operation and duplicate submission. ---
  await pick('#pv-a');
  r = await step(async () => {
    const v = globalThis.__v;
    const engine = v.pane().studioPreview().engine;
    let applies = 0;
    const original = engine.apply.bind(engine);
    engine.apply = (...args) => {
      applies++;
      return original(...args);
    };
    v.button('studio-apply').click();
    v.button('studio-apply').click();
    // The selection moves on while the apply is in flight.
    const doc = await v.domModel().requestDocument();
    v.Common.Revealer.reveal(v.domModel().nodeForId(await v.domModel().querySelector(doc.id, '#pv-b')));
    await v.settle(1500);
    engine.apply = original;
    return {applies, state: v.state(), status: v.previewStatus()};
  });
  check('a double click submits once', r.applies === 1, JSON.stringify(r));
  check('an apply overtaken by a selection change leaves no preview on either element',
        r.state === 'none' && await color('#pv-a') === 'rgb(10, 10, 10)' && await color('#pv-b') === 'rgb(10, 10, 10)',
        JSON.stringify(r));

  // --- 8. Navigation. ---
  await pick('#pv-a');
  await clickButton('studio-apply');
  const navApplied = await color('#pv-a') === 'rgb(154, 0, 0)';
  await page.reload();
  await sleep(2500);
  r = await ui();
  check('a reload removes the preview with the old document, says so, and keeps the draft',
        navApplied && r.state === 'none' && /navigated|left the page|target or scope changed/.test(r.status) &&
            r.draft === 'color: rgb(154, 0, 0)' && await color('#pv-a') === 'rgb(10, 10, 10)',
        JSON.stringify(r));
  check('nothing is left in the ownership record after the navigation',
        (await step(async () => await globalThis.__v.ledgerFor())) === null);

  // --- 9. Normal close and reopen. ---
  const pageTargetId = page.target()._targetId;
  const browserSession = await browser.target().createCDPSession();
  const reopen = async () => {
    const {targetId} = await browserSession.send('Target.openDevTools', {targetId: pageTargetId});
    const target = await browser.waitForTarget(t => t._targetId === targetId, {timeout: 30000});
    devtools = await target.asPage();
    devtools.on('pageerror', e => errors.push(e.message));
    await setUp(devtools);
    await sleep(1500);
  };
  await pick('#pv-a');
  await clickButton('studio-apply');
  const closeApplied = await color('#pv-a') === 'rgb(154, 0, 0)';
  await devtools.close();
  await sleep(1500);
  const afterClose = await color('#pv-a');
  await reopen();
  r = await step(async () => ({items: globalThis.__v.recoveryItems(), ledger: await globalThis.__v.ledgerFor()}));
  check('a normal close releases the preview, and the reopened DevTools offers nothing and drops the record',
        closeApplied && afterClose === 'rgb(10, 10, 10)' && r.items.length === 0 && r.ledger === null,
        JSON.stringify({afterClose, ...r}));

  // --- 10. Abrupt disconnect: DevTools killed with a preview applied. ---
  await pick('#pv-a');
  await typeDraft('color: rgb(160, 0, 0)');
  await clickButton('studio-apply');
  // The user's Styles-pane rule, and another feature's forced sheet with Studio-like text.
  await step(async () => {
    const v = globalThis.__v;
    const frameId = v.SDK.TargetManager.TargetManager.instance().primaryPageTarget()
                        .model(v.SDK.ResourceTreeModel.ResourceTreeModel).mainFrame.id;
    const user = await v.cssModel().requestViaInspectorStylesheet(frameId);
    await v.cssModel().setStyleSheetText(user.id, '#pv-c { outline-color: rgb(0, 0, 77); }\n', false);
    const other = await v.cssModel().createInspectorStylesheet(frameId, true);
    await v.cssModel().setStyleSheetText(
        other.id, '/* avora-style-studio-preview v1 */\n#pv-b { color: rgb(161, 0, 0); }\n', true);
  });
  const crashedApplied = await color('#pv-a') === 'rgb(160, 0, 0)';
  const devtoolsTargetId = devtools.target()._targetId;
  const crashSession = await devtools.createCDPSession();
  await crashSession.send('Page.crash').catch(() => {});
  await sleep(1000);
  await browserSession.send('Target.closeTarget', {targetId: devtoolsTargetId}).catch(() => {});
  await sleep(1500);
  check('killing DevTools leaves the preview applied (no release could run)',
        crashedApplied && await color('#pv-a') === 'rgb(160, 0, 0)');
  await reopen();
  r = await step(async () => {
    const v = globalThis.__v;
    await v.waitFor(() => v.recoveryItems().length > 0);
    return {items: v.recoveryItems(), text: v.text('.studio-recovery')};
  });
  check('the reopened DevTools reports the earlier preview, and only Studio\'s own sheet',
        r.items.length === 1 && r.items[0].kind === 'orphan' && r.items[0].css.includes('rgb(160, 0, 0)') &&
            /earlier DevTools session/.test(r.text ?? ''),
        JSON.stringify(r));
  await clickButton('studio-recovery-keep');
  r = await step(async () => ({items: globalThis.__v.recoveryItems(), ledger: await globalThis.__v.ledgerFor()}));
  check('Keep leaves the sheet applied and its record in place', r.items.length === 0 &&
        await color('#pv-a') === 'rgb(160, 0, 0)' && r.ledger?.entries?.length === 1, JSON.stringify(r));
  await devtools.close();
  await sleep(1500);
  await reopen();
  r = await step(async () => {
    const v = globalThis.__v;
    await v.waitFor(() => v.recoveryItems().length > 0);
    return {items: v.recoveryItems()};
  });
  check('a later session asks again about a kept preview', r.items.length === 1, JSON.stringify(r));
  await clickButton('studio-recovery-remove');
  r = await step(async () => ({
    items: globalThis.__v.recoveryItems(),
    result: globalThis.__v.text('.studio-recovery .studio-status'),
    ledger: await globalThis.__v.ledgerFor(),
  }));
  check('Remove clears exactly the recorded sheet, confirms it, and then deletes the record',
        await color('#pv-a') === 'rgb(10, 10, 10)' && r.items.length === 0 && r.ledger === null &&
            /earlier preview was removed/.test(r.result ?? ''),
        JSON.stringify(r));
  check('the user\'s default-sheet rule and the other feature\'s forced sheet are untouched',
        await color('#pv-c', 'outline-color') === 'rgb(0, 0, 77)' && await color('#pv-b') === 'rgb(161, 0, 0)');
} finally {
  server.close();
  fs.writeFileSync(path.join(outDir, 'results.json'), JSON.stringify(results, null, 2));
  await browser.close();
}
console.log('DevTools page errors:', JSON.stringify(errors.filter(e => /style_intelligence|StyleStudio/i.test(e))));
const failed = results.filter(r => !r.ok).length;
console.log(`\n${results.length - failed}/${results.length} checks passed`);
process.exit(failed ? 1 : 0);
