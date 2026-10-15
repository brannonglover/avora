// Live verification of Style Studio Checkpoint 1A against a built Avora.
//
// Launches Avora with DevTools docked, opens style-studio-targeting.html, and
// drives the real Elements selection and Intelligence pane. Exits non-zero if
// any check fails. Screenshots and results.json go to <out-dir>.
//
// Usage (from the repo root, after `npm run build`):
//   chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
//     docs/style-intelligence/fixtures/verify-style-studio-targeting.mjs <out-dir>
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const PUPPETEER = path.join(
    ROOT, 'chromium/src/third_party/devtools-frontend/src/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js');
const {default: puppeteer} = await import(PUPPETEER);

const binary = path.join(ROOT, 'chromium/src/out/Default/Avora.app/Contents/MacOS/Avora');
const fixture = path.join(HERE, 'style-studio-targeting.html');
const outDir = path.resolve(process.argv[2] ?? 'style-studio-verify-out');
fs.mkdirSync(outDir, {recursive: true});
const results = [];
function check(name, ok, detail = '') {
  results.push({name, ok, detail});
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  — ${detail}` : ''}`);
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

const browser = await puppeteer.launch({
  executablePath: binary,
  headless: false,
  devtools: true,
  defaultViewport: null,
  userDataDir: path.join(outDir, 'profile'),
  args: ['--no-first-run', '--no-default-browser-check', '--window-size=1500,1000'],
});

try {
  const [page] = await browser.pages();
  await page.goto('file://' + fixture);
  const devtoolsTarget = await browser.waitForTarget(t => t.url().startsWith('devtools://'), {timeout: 30000});
  const devtools = await devtoolsTarget.asPage();
  await devtools.waitForFunction(() => document.readyState === 'complete', {timeout: 30000});
  const devtoolsErrors = [];
  devtools.on('console', m => { if (m.type() === 'error') devtoolsErrors.push(m.text()); });
  devtools.on('pageerror', e => devtoolsErrors.push('pageerror: ' + e.message));
  globalThis.__devtoolsErrors = devtoolsErrors;
  await sleep(3000);

  // Everything below runs inside the real DevTools frontend. Modules are
  // imported by URL so they are the same instances DevTools is using.
  const setup = await devtools.evaluate(async () => {
    const UI = await import('./ui/legacy/legacy.js');
    const SDK = await import('./core/sdk/sdk.js');
    await UI.ViewManager.ViewManager.instance().showView('elements');
    await UI.ViewManager.ViewManager.instance().showView('elements.style-intelligence');
    const SI = await import('./panels/style_intelligence/style_intelligence.js');
    const pane = SI.StyleIntelligencePane.StyleIntelligencePane.instance();
    const target = SDK.TargetManager.TargetManager.instance().primaryPageTarget();
    const domModel = target.model(SDK.DOMModel.DOMModel);
    const doc = await domModel.requestDocument();
    globalThis.__v = {UI, SDK, pane, domModel, doc};
    return {paneShown: pane.isShowing(), docId: doc.id};
  });
  check('Intelligence pane is shown in the Elements sidebar', setup.paneShown);

  // Helpers installed in the DevTools page.
  await devtools.evaluate(() => {
    const v = globalThis.__v;
    const Common = import('./core/common/common.js');
    v.root = () => v.pane.contentElement;
    v.studio = () => v.root().querySelector('.studio');
    v.status = () => v.studio()?.querySelector('.studio-status')?.textContent.replace(/\s+/g, ' ').trim() ?? null;
    v.identity = () => v.root().querySelector('.studio-panel .identity')?.textContent.trim() ?? null;
    v.tab = name => v.root().querySelector(`#tab-${name}`);
    v.openTab = async name => {
      v.tab(name).click();
      await new Promise(r => setTimeout(r, 200));
    };
    v.selected = () => v.UI.Context.Context.instance().flavor(v.SDK.DOMModel.DOMNode);
    v.select = async selector => {
      const nodeId = await v.domModel.querySelector(v.doc.id, selector);
      const node = v.domModel.nodeForId(nodeId);
      await (await Common).Revealer.reveal(node);
      return node.id;
    };
    v.radio = (group, index) => {
      const inputs = v.studio().querySelectorAll(`input[name="${group}"]`);
      inputs[index].click();
    };
    v.type = text => {
      const input = v.studio().querySelector('.studio-selector');
      input.value = text;
      input.dispatchEvent(new Event('input', {bubbles: true}));
      input.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
    };
    v.settle = () => new Promise(r => setTimeout(r, 600));
  });

  const step = async (fn, arg) => devtools.evaluate(fn, arg);

  const mark = label => page.evaluate(l => window.__studioMutationLog?.push(`--- ${l}`), label);
  // Parser mutations after the observer attached are not Studio's; baseline here.
  await page.evaluate(() => { window.__studioMutations = 0; window.__studioMutationLog = []; });
  await mark('start');
  // 0. Tabs: Explain and Studio are separate, keyboard reachable views.
  let r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#unique');
    await v.settle();
    await v.openTab('explain');
    const explain = {studio: Boolean(v.studio()), plate: Boolean(v.root().querySelector('.plate'))};
    await v.openTab('studio');
    const studio = {studio: Boolean(v.studio()), plate: Boolean(v.root().querySelector('.plate'))};
    const tabs = [...v.root().querySelectorAll('[role="tab"]')].map(t => `${t.textContent.trim()}:${t.getAttribute('aria-selected')}`);
    v.tab('studio').dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowLeft', bubbles: true}));
    await new Promise(res => setTimeout(res, 200));
    const afterArrow = v.root().querySelector('[role="tab"][aria-selected="true"]')?.id;
    const focused = v.root().getRootNode().activeElement?.id ?? null;
    await v.openTab('studio');
    return {explain, studio, tabs, afterArrow, focused};
  });
  check('Explain tab shows the explanation and no Studio', r.explain.plate && !r.explain.studio, JSON.stringify(r.explain));
  check('Studio tab shows Studio and no explanation', r.studio.studio && !r.studio.plate, JSON.stringify(r.studio));
  check('ArrowLeft moves selection and focus to the Explain tab', r.afterArrow === 'tab-explain' && r.focused === 'tab-explain',
        `${r.tabs.join(', ')} → ${r.afterArrow}, focus ${r.focused}`);

  // 1. Selected-node synchronization.
  r = await step(async () => {
    const v = globalThis.__v;
    const id = await v.select('#unique');
    await v.settle();
    return {id, flavor: v.selected()?.id, status: v.status(), identity: v.identity()};
  });
  check('selecting #unique in Elements syncs the Studio target',
        r.flavor === r.id && r.identity === 'div#unique.card' && /This element only/.test(r.status ?? ''),
        `${r.identity} · ${r.status}`);
  await devtools.evaluate(() => globalThis.__v.studio()?.scrollIntoView()); await devtools.screenshot({path: path.join(outDir, '1-selected-node.png')});

  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('.shared');
    await v.settle();
    return {identity: v.identity()};
  });
  check('changing the Elements selection updates the Studio target', r.identity === 'div.card.shared', r.identity ?? '');

  await mark('// 2.');
  // 2. Switch to selector targeting; prefill and one-node scope.
  r = await step(async () => {
    const v = globalThis.__v;
    const before = v.selected().id;
    v.radio('studio-source', 1);
    await v.settle();
    const input = v.studio().querySelector('.studio-selector');
    const scopeChecked = [...v.studio().querySelectorAll('input[name="studio-scope"]')].map(i => i.checked);
    return {before, after: v.selected().id, prefill: input?.value, scopeChecked, status: v.status()};
  });
  check('switching to Selector prefills a selector and keeps one-node scope',
        Boolean(r.prefill) && r.scopeChecked[0] === true && r.scopeChecked[1] === false, `${r.prefill} · ${r.status}`);

  await mark('// 3.');
  // 3. Valid selector, multiple matches; scope stays one node.
  r = await step(async () => {
    const v = globalThis.__v;
    v.type('.card');
    await v.settle();
    return {status: v.status()};
  });
  check('.card (4 matches) keeps one-node scope until All matches is chosen',
        /This element only/.test(r.status ?? '') && /4 elements/.test(r.status ?? ''), r.status);

  r = await step(async () => {
    const v = globalThis.__v;
    v.radio('studio-scope', 1);
    await v.settle();
    return {status: v.status()};
  });
  check('All matches targets 4 elements (shadow card is not counted)', /^4 matching elements$/.test(r.status ?? ''), r.status);
  await devtools.evaluate(() => globalThis.__v.studio()?.scrollIntoView()); await devtools.screenshot({path: path.join(outDir, '2-all-matches.png')});

  await mark('// 4.');
  // 4. Valid selector, one match, not the selected node.
  r = await step(async () => {
    const v = globalThis.__v;
    v.radio('studio-scope', 0);
    v.type('#unique');
    await v.settle();
    return {status: v.status()};
  });
  check('#unique while .shared is selected is not substituted for the selection',
        /matches 1 element, but not the selected one/.test(r.status ?? ''), r.status);

  await mark('// 5.');
  // 5. Zero matches.
  r = await step(async () => {
    const v = globalThis.__v;
    v.type('.nothing-here');
    await v.settle();
    return {status: v.status()};
  });
  check('valid selector with zero matches', /No elements in this document match/.test(r.status ?? ''), r.status);

  await mark('// 6.');
  // 6. Invalid selector.
  r = await step(async () => {
    const v = globalThis.__v;
    v.type('a[');
    await v.settle();
    return {status: v.status(), error: v.studio().querySelector('.studio-status').classList.contains('error')};
  });
  check('invalid selector syntax is reported as an error', r.error && /Invalid selector/.test(r.status ?? ''), r.status);
  await devtools.evaluate(() => globalThis.__v.studio()?.scrollIntoView()); await devtools.screenshot({path: path.join(outDir, '3-invalid.png')});

  await mark('// 7.');
  // 7. Back to selected-node; scope resets on return to Selector.
  r = await step(async () => {
    const v = globalThis.__v;
    v.type('.card');
    await v.settle();
    v.radio('studio-scope', 1);
    await v.settle();
    v.radio('studio-source', 0);
    await v.settle();
    const nodeStatus = v.status();
    v.radio('studio-source', 1);
    await v.settle();
    const scopeChecked = [...v.studio().querySelectorAll('input[name="studio-scope"]')].map(i => i.checked);
    return {nodeStatus, scopeChecked, status: v.status()};
  });
  check('returning to Selector resets scope to one node', r.scopeChecked[0] === true && /This element only/.test(r.status ?? ''),
        `${r.nodeStatus} → ${r.status}`);

  await mark('// 8.');
  // 8. Iframe document context.
  r = await step(async () => {
    const v = globalThis.__v;
    const frameOwnerId = await v.domModel.querySelector(v.doc.id, '#frame');
    const owner = v.domModel.nodeForId(frameOwnerId);
    await owner.getChildNodesPromise();
    const frameDoc = owner.contentDocument();
    if (!frameDoc) {
      return {skipped: true};
    }
    const pId = await v.domModel.querySelector(frameDoc.id, '.in-frame');
    const Common = await import('./core/common/common.js');
    await Common.Revealer.reveal(v.domModel.nodeForId(pId));
    await v.settle();
    v.type('p');
    await v.settle();
    v.radio('studio-scope', 1);
    await v.settle();
    return {status: v.status()};
  });
  check('selector resolves in the iframe document of the selected node', !r.skipped && /^2 matching elements$/.test(r.status ?? ''),
        r.skipped ? 'iframe document not available' : r.status);

  await mark('// 9.');
  // 9. Shadow DOM is declined.
  r = await step(async () => {
    const v = globalThis.__v;
    v.radio('studio-source', 0);
    const hostId = await v.domModel.querySelector(v.doc.id, '#host');
    const host = v.domModel.nodeForId(hostId);
    await host.getChildNodesPromise();
    const shadowRoot = host.shadowRoots()[0];
    if (!shadowRoot) {
      return {skipped: true};
    }
    const innerId = await v.domModel.querySelector(shadowRoot.id, '.inner');
    const Common = await import('./core/common/common.js');
    await Common.Revealer.reveal(v.domModel.nodeForId(innerId));
    await v.settle();
    return {status: v.status()};
  });
  check('an element inside a shadow root is declined explicitly', !r.skipped && /shadow root are not supported/.test(r.status ?? ''),
        r.skipped ? 'shadow root not available' : r.status);
  await devtools.evaluate(() => globalThis.__v.studio()?.scrollIntoView()); await devtools.screenshot({path: path.join(outDir, '4-shadow.png')});

  // 9b. Picking on the page through the existing inspect mode.
  await mark('// 9b.');
  const centerOf = async selector => page.evaluate(sel => {
    const [outer, inner] = sel.split(' >>> ');
    let el = document.querySelector(outer);
    if (inner) {
      el = el.shadowRoot.querySelector(inner);
    }
    const r = el.getBoundingClientRect();
    return {x: r.x + Math.min(40, r.width / 2), y: r.y + r.height / 2};
  }, selector);
  const studioState = () => step(() => {
    const v = globalThis.__v;
    const input = v.studio().querySelector('.studio-selector');
    const selected = v.selected();
    return {
      selected: selected ? (selected.getAttribute('id') ?? selected.getAttribute('class') ?? selected.nodeName()) : null,
      identity: v.root().querySelector('.studio-panel .identity')?.textContent.trim() ?? null,
      selector: input?.value ?? null,
      scope: [...v.studio().querySelectorAll('input[name="studio-scope"]')].map(i => i.checked),
      pressed: v.studio().querySelector('.studio-pick')?.getAttribute('aria-pressed') ?? null,
      hint: v.studio().querySelector('.studio-hint[role="status"]')?.textContent.trim() ?? null,
      status: v.status(),
    };
  });
  const startStudioPick = () => step(async () => {
    const v = globalThis.__v;
    v.studio().querySelector('.studio-pick').click();
    await v.settle();
  });
  const pickAt = async selector => {
    const point = await centerOf(selector);
    await page.mouse.move(point.x, point.y);
    await page.mouse.click(point.x, point.y);
    await sleep(1000);
  };

  await step(async () => {
    const v = globalThis.__v;
    await v.select('#unique');
    await v.settle();
    v.radio('studio-source', 1);
    await v.settle();
    v.type('p');
    await v.settle();
    v.radio('studio-scope', 1);
    await v.settle();
  });
  await startStudioPick();
  r = await studioState();
  check('pick button enters inspect mode and says how to cancel', r.pressed === 'true' && /Esc/.test(r.hint ?? ''),
        `${r.pressed} · ${r.hint}`);
  await pickAt('.shared');
  r = await studioState();
  check('picking a card selects it and fills a readable selector in one-node scope',
        r.selected === 'card shared' && r.selector === 'div.card.shared' && r.scope[0] === true && r.pressed === 'false' &&
            /matches 3 elements/.test(r.status ?? ''),
        `${r.selector} · ${r.status}`);

  await startStudioPick();
  await pickAt('#unique');
  r = await studioState();
  check('picking an element with an id fills #id', r.selected === 'unique' && r.selector === '#unique' &&
        /matches 1 element/.test(r.status ?? ''), `${r.selector} · ${r.status}`);

  // Repeated pick: #unique is already selected. Change the selector and scope, then pick #unique again.
  for (let attempt = 1; attempt <= 2; attempt++) {
    await step(async () => {
      const v = globalThis.__v;
      v.type('.card');
      await v.settle();
      v.radio('studio-scope', 1);
      await v.settle();
    });
    await startStudioPick();
    await pickAt('#unique');
    r = await studioState();
    check(`re-picking the already-selected element refills the selector (attempt ${attempt})`,
          r.selected === 'unique' && r.selector === '#unique' && r.scope[0] === true && r.pressed === 'false',
          `${r.selector} · scope ${JSON.stringify(r.scope)} · pressed ${r.pressed}`);
  }

  // Cancel keeps the previous selector and an explicit All matches scope.
  await step(async () => {
    const v = globalThis.__v;
    v.type('.card');
    await v.settle();
    v.radio('studio-scope', 1);
    await v.settle();
  });
  await startStudioPick();
  await devtools.keyboard.press('Escape');
  await sleep(800);
  r = await studioState();
  check('Esc cancels picking and keeps the selection, selector, and scope',
        r.pressed === 'false' && r.selector === '.card' && r.scope[1] === true && r.selected === 'unique' &&
            r.status === '4 matching elements',
        `${r.pressed} · ${r.selector} · ${r.status}`);

  // A pick started from the Elements toolbar picker selects the element but leaves the typed selector alone.
  await step(async () => {
    const v = globalThis.__v;
    await v.UI.ActionRegistry.ActionRegistry.instance().getAction('elements.toggle-element-search').execute();
    await v.settle();
  });
  await pickAt('.lonely');
  r = await studioState();
  check('a toolbar pick changes the selection but not the typed selector', r.selected === 'lonely' && r.selector === '.card',
        `${r.selected} · ${r.selector}`);

  // Picking an unsupported element gives explicit feedback.
  await step(async () => {
    const v = globalThis.__v;
    v.radio('studio-scope', 0);
    await v.settle();
  });
  await startStudioPick();
  await pickAt('#host >>> .inner');
  r = await studioState();
  check('picking an element inside a shadow root reports it as unsupported',
        r.pressed === 'false' && /shadow root are not supported/.test(r.status ?? ''), `${r.identity} · ${r.status}`);

  // Nothing selected: the picker is still offered, and a pick establishes the first target.
  r = await step(async () => {
    const v = globalThis.__v;
    v.UI.Context.Context.instance().setFlavor(v.SDK.DOMModel.DOMNode, null);
    await v.settle();
    return {
      identity: v.root().querySelector('.studio-panel .identity')?.textContent.trim() ?? null,
      pick: Boolean(v.studio()?.querySelector('.studio-pick')),
      status: v.status(),
    };
  });
  check('with nothing selected, Studio still offers the picker', r.pick && r.identity === 'No element selected' &&
        /Pick an element on the page/.test(r.status ?? ''), `${r.identity} · ${r.status}`);
  await startStudioPick();
  await pickAt('.lonely');
  r = await studioState();
  check('a first pick with nothing selected establishes the target and selector',
        r.selected === 'lonely' && r.identity === 'p.lonely' && r.selector === 'p.lonely' && /matches 1 element/.test(r.status ?? ''),
        `${r.identity} · ${r.selector} · ${r.status}`);

  const pageClicks = await page.evaluate(() => window.__studioPageClicks);
  check('picked clicks never reached the page', pageClicks === 0, `clicks=${pageClicks}`);

  await mark('// 10.');
  // 10. The page saw no DOM or stylesheet changes, and Style Intelligence still rendered.
  const pageState = await page.evaluate(() => ({log: window.__studioMutationLog, mutations: window.__studioMutations, sheets: document.styleSheets.length, initialSheets: window.__studioSheets}));
  check('page DOM was not mutated by targeting', pageState.log.every(l => l.startsWith('---')), `mutations=${pageState.mutations} ${JSON.stringify(pageState.log)}`);
  check('page stylesheets were not changed', pageState.sheets === pageState.initialSheets, `sheets=${pageState.sheets}`);
  r = await step(async () => {
    const v = globalThis.__v;
    await v.select('#unique');
    await v.openTab('explain');
    await v.settle();
    return {hasChain: Boolean(v.root().querySelector('header .identity')), hasPlate: Boolean(v.root().querySelector('.plate'))};
  });
  check('existing Style Intelligence content still renders', r.hasChain && r.hasPlate, JSON.stringify(r));
  const pageConsole = await devtools.evaluate(async () => {
    const SDK = await import('./core/sdk/sdk.js');
    return SDK.TargetManager.TargetManager.instance().primaryPageTarget().model(SDK.ConsoleModel.ConsoleModel).messages().filter(m => m.level === 'error').map(m => `${m.messageText} @ ${m.url ?? ''}`);
  });
  console.log('inspected page console errors:', JSON.stringify(pageConsole));
  console.log('DevTools frontend errors:', JSON.stringify(globalThis.__devtoolsErrors));
  check('no DevTools frontend errors from Style Studio', !globalThis.__devtoolsErrors.some(t => /style_intelligence|StyleStudio/i.test(t)), String(globalThis.__devtoolsErrors.length));
} finally {
  fs.writeFileSync(path.join(outDir, 'results.json'), JSON.stringify(results, null, 2));
  await browser.close();
}
const failed = results.filter(r => !r.ok).length;
console.log(`\n${results.length - failed}/${results.length} checks passed`);
process.exit(failed ? 1 : 0);
