// Raw CDP verification of inspector-stylesheet text isolation.
//
// Blink used to keep one stored text per document for every inspector
// stylesheet (CSS.createStyleSheet), so a second inspector sheet corrupted the
// first: a new sheet inherited its text, and after a reconnect the Styles
// pane's "New Style Rule" could delete the user's earlier rules. This script
// drives a built Avora over plain CDP sessions (each one is what a DevTools
// session is to Blink) and checks stylesheet text, real rule counts, and
// computed styles in the page, not only protocol responses.
//
// Usage (from the repo root, after `npm run build`):
//   chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
//     docs/style-intelligence/fixtures/repro-inspector-stylesheet-text-slot.mjs [out-dir]
// Exits non-zero if any check fails.
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));  // docs/style-intelligence/fixtures
const ROOT = path.resolve(HERE, '../../..');
const {default: puppeteer} = await import(path.join(
    ROOT, 'chromium/src/third_party/devtools-frontend/src/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const out = path.resolve(process.argv[2] ?? 'inspector-stylesheet-text-slot-out');
fs.mkdirSync(out, {recursive: true});

// A self-contained page: elements with known base styles for the checks.
const PAGE = `<!doctype html><meta charset="utf-8"><title>inspector stylesheet isolation</title>
<style>
  body { color: rgb(0, 0, 0); }
  .item { color: rgb(10, 10, 10); }
</style>
<div class="item" id="pv-a">a</div>
<div class="item" id="pv-b">b</div>
<div class="item" id="pv-c">c</div>
<div id="pv-plain">plain</div>`;
const server = http.createServer((request, response) => {
  response.writeHead(200, {'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store'});
  response.end(PAGE);
});
await new Promise(resolve => server.listen(0, 'localhost', resolve));
const url = `http://localhost:${server.address().port}/`;

const results = [];
function check(name, ok, detail = '') {
  results.push({name, ok});
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  — ${detail}` : ''}`);
}

const browser = await puppeteer.launch({
  executablePath: path.join(ROOT, 'chromium/src/out/Default/Avora.app/Contents/MacOS/Avora'),
  headless: false,
  defaultViewport: null,
  userDataDir: path.join(out, 'profile'),
  args: ['--no-first-run', '--no-default-browser-check'],
});

const RULE = {
  user: '#pv-c { outline-color: rgb(0, 0, 77); }',
  second: '#pv-a { color: rgb(124, 0, 0); }',
  plus: '#pv-b { outline-color: rgb(0, 77, 0); }',
  third: '#pv-plain { color: rgb(0, 0, 99); }',
};

/** A CDP session with DOM and CSS enabled: one DevTools session, to Blink. */
async function connect(page) {
  const session = await page.target().createCDPSession();
  const added = [];
  session.on('CSS.styleSheetAdded', ({header}) => added.push(header));
  await session.send('DOM.enable');
  await session.send('CSS.enable');
  const {frameTree} = await session.send('Page.getFrameTree');
  return {
    session,
    frameId: frameTree.frame.id,
    inspectorIds: () => added.filter(header => header.origin === 'inspector').map(header => header.styleSheetId),
    create: async force => (await session.send('CSS.createStyleSheet', {frameId: frameTree.frame.id, force})).styleSheetId,
    text: async id => (await session.send('CSS.getStyleSheetText', {styleSheetId: id})).text,
    setText: (id, text) => session.send('CSS.setStyleSheetText', {styleSheetId: id, text}),
  };
}

const computed = (page, selector, property) => page.evaluate(
    (s, p) => getComputedStyle(document.querySelector(s)).getPropertyValue(p).trim(), selector, property);

async function freshPage() {
  const page = await browser.newPage();
  await page.goto(url);
  return page;
}

try {
  // --- The ten-step scenario, default sheet first. ---
  {
    const page = await freshPage();
    const s1 = await connect(page);
    const user = await s1.create(false);                                     // 1
    await s1.setText(user, RULE.user);                                       // 2
    const second = await s1.create(true);                                    // 3
    check('4. a new second sheet does not inherit the user sheet\'s text', await s1.text(second) === '',
          JSON.stringify(await s1.text(second)));
    await s1.setText(second, RULE.second);                                   // 5
    check('5. both sheets hold their own text', await s1.text(user) === RULE.user && await s1.text(second) === RULE.second);
    check('5. both sheets are in effect', await computed(page, '#pv-c', 'outline-color') === 'rgb(0, 0, 77)' &&
          await computed(page, '#pv-a', 'color') === 'rgb(124, 0, 0)');
    await s1.session.detach();                                               // 6 (close)

    const s2 = await connect(page);                                          // 6 (reopen)
    const ids = s2.inspectorIds();
    const texts = await Promise.all(ids.map(id => s2.text(id)));
    check('7. after reopening, each sheet still reports its own text',
          ids.length === 2 && texts.includes(RULE.user) && texts.includes(RULE.second), JSON.stringify(texts));
    // Sheets are told apart by their own text: a reconnecting session gets them
    // in hash order (InspectorCSSAgent::SetActiveStyleSheets), not creation order.
    const userAgain = ids[texts.indexOf(RULE.user)];
    const secondAgain = ids[texts.indexOf(RULE.second)];
    // 8. "New Style Rule" as the frontend does it (Checkpoint 2A.2): ask the
    // backend for the frame's default inspector sheet, then append to it.
    const first = await s2.create(false);
    check('8. "New Style Rule" after reopening uses the user\'s default sheet', first === userAgain && ids.length === 2,
          `${first} vs ${userAgain}`);
    await s2.setText(first, `${await s2.text(first)}\n${RULE.plus}`);
    check('9. the original user rule survives a Styles-pane rule after reopening',
          await computed(page, '#pv-c', 'outline-color') === 'rgb(0, 0, 77)' &&
              await computed(page, '#pv-b', 'outline-color') === 'rgb(0, 77, 0)' &&
              (await s2.text(userAgain)).startsWith(RULE.user),
          `#pv-c ${await computed(page, '#pv-c', 'outline-color')}`);
    const userBefore = await s2.text(userAgain);
    await s2.setText(secondAgain, '');                                       // 10
    check('10. clearing the second sheet leaves the user sheet unchanged',
          await s2.text(userAgain) === userBefore && await computed(page, '#pv-a', 'color') === 'rgb(10, 10, 10)' &&
              await computed(page, '#pv-c', 'outline-color') === 'rgb(0, 0, 77)',
          JSON.stringify(await s2.text(userAgain)));
    await s2.session.detach();
    await page.close();
  }

  // --- Reverse creation order: the extra sheet first. ---
  {
    const page = await freshPage();
    const s1 = await connect(page);
    const second = await s1.create(true);
    await s1.setText(second, RULE.second);
    const user = await s1.create(false);
    check('reverse order: the default sheet created second starts empty', await s1.text(user) === '');
    await s1.setText(user, RULE.user);
    await s1.session.detach();
    const s2 = await connect(page);
    const texts = await Promise.all(s2.inspectorIds().map(id => s2.text(id)));
    check('reverse order: both texts survive a reconnect',
          JSON.stringify([...texts].sort()) === JSON.stringify([RULE.second, RULE.user].sort()), JSON.stringify(texts));
    check('reverse order: both sheets are in effect', await computed(page, '#pv-c', 'outline-color') === 'rgb(0, 0, 77)' &&
          await computed(page, '#pv-a', 'color') === 'rgb(124, 0, 0)');
    await s2.session.detach();
    await page.close();
  }

  // --- Several additional inspector sheets. ---
  {
    const page = await freshPage();
    const s1 = await connect(page);
    const user = await s1.create(false);
    await s1.setText(user, RULE.user);
    const extra = [];
    for (let i = 0; i < 4; i++) {
      const id = await s1.create(true);
      check(`multiple: extra sheet ${i + 1} starts empty`, await s1.text(id) === '');
      await s1.setText(id, `#pv-plain { margin-left: ${i + 1}px; }`);
      extra.push(id);
    }
    await s1.setText(extra[2], RULE.third);
    await s1.session.detach();
    const s2 = await connect(page);
    const texts = await Promise.all(s2.inspectorIds().map(id => s2.text(id)));
    check('multiple: every sheet keeps its own text across a reconnect',
          JSON.stringify([...texts].sort()) ===
              JSON.stringify([RULE.user, '#pv-plain { margin-left: 1px; }', '#pv-plain { margin-left: 2px; }', RULE.third,
                              '#pv-plain { margin-left: 4px; }'].sort()),
          JSON.stringify(texts));
    check('multiple: the page reflects every sheet', await computed(page, '#pv-plain', 'margin-left') === '4px' &&
          await computed(page, '#pv-plain', 'color') === 'rgb(0, 0, 99)' &&
          await computed(page, '#pv-c', 'outline-color') === 'rgb(0, 0, 77)');
    await s2.session.detach();
    await page.close();
  }

  // --- A session dropped without disabling CSS, then reconnect. ---
  {
    const page = await freshPage();
    const s1 = await connect(page);
    const user = await s1.create(false);
    await s1.setText(user, RULE.user);
    const second = await s1.create(true);
    await s1.setText(second, RULE.second);
    await s1.session.detach();   // No CSS.disable: an abrupt disconnect.
    const s2 = await connect(page);
    const texts = await Promise.all(s2.inspectorIds().map(id => s2.text(id)));
    check('abrupt disconnect: texts survive unchanged',
          JSON.stringify([...texts].sort()) === JSON.stringify([RULE.user, RULE.second].sort()), JSON.stringify(texts));
    await s2.session.detach();
    // After a reload the documents and their inspector sheets are gone.
    await page.reload();
    const s3 = await connect(page);
    check('navigation: a reloaded page has no inspector sheets left', s3.inspectorIds().length === 0 &&
          await computed(page, '#pv-a', 'color') === 'rgb(10, 10, 10)');
    const fresh = await s3.create(true);
    check('navigation: a sheet in the new document starts empty', await s3.text(fresh) === '');
    await s3.session.detach();
    await page.close();
  }
} finally {
  server.close();
  await browser.close();
}
const failed = results.filter(result => !result.ok).length;
console.log(`\n${results.length - failed}/${results.length} checks passed`);
process.exit(failed ? 1 : 0);
