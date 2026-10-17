// Browser-level check of the Styles pane's default inspector stylesheet with
// the real DevTools frontend (Checkpoint 2A.2).
//
// CSSModel.requestViaInspectorStylesheet is what "New Style Rule" and the
// Elements view-transition helper call. It must return the frame's canonical
// default even when another feature's forced inspector stylesheet exists
// first, share one default between concurrent requests, and return the same
// default (with the user's rules) after DevTools is closed and reopened.
// Reopened sessions connect over remote debugging, so the whole frontend
// starts fresh each time.
//
// Usage (from the repo root, after `npm run build`):
//   chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
//     docs/style-intelligence/fixtures/verify-inspector-default-stylesheet-devtools.mjs [out-dir]
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));  // docs/style-intelligence/fixtures
const ROOT = path.resolve(HERE, '../../..');
const {default: puppeteer} = await import(path.join(
    ROOT, 'chromium/src/third_party/devtools-frontend/src/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const out = path.resolve(process.argv[2] ?? 'inspector-default-devtools-out');
fs.mkdirSync(out, {recursive: true});

const PAGE = `<!doctype html><meta charset="utf-8"><title>default inspector stylesheet</title>
<style>body { color: rgb(0, 0, 0); }</style><div id="a">a</div><div id="b">b</div><div id="c">c</div>`;
const server = http.createServer((request, response) => {
  response.writeHead(200, {'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store'});
  response.end(PAGE);
});
await new Promise(resolve => server.listen(0, 'localhost', resolve));
const url = `http://localhost:${server.address().port}/`;
const DEBUG_PORT = 9000 + Math.floor(Math.random() * 900);

const results = [];
function check(name, ok, detail = '') {
  results.push({name, ok});
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  — ${detail}` : ''}`);
}
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

const browser = await puppeteer.launch({
  executablePath: path.join(ROOT, 'chromium/src/out/Default/Avora.app/Contents/MacOS/Avora'),
  headless: false,
  devtools: true,
  defaultViewport: null,
  userDataDir: path.join(out, 'profile'),
  args: ['--no-first-run', '--no-default-browser-check', `--remote-debugging-port=${DEBUG_PORT}`,
         `--remote-allow-origins=http://localhost:${DEBUG_PORT}`],
});
const color = (page, id) => page.evaluate(i => getComputedStyle(document.getElementById(i)).color, id);

/** Runs in a DevTools frontend: the primary target's CSSModel, main frame, and inspector sheets. */
const frontendHelpers = async () => {
  const SDK = await import('./core/sdk/sdk.js');
  // A freshly opened frontend attaches its target and main frame asynchronously.
  const ready = () => SDK.TargetManager.TargetManager.instance().primaryPageTarget()
                          ?.model(SDK.ResourceTreeModel.ResourceTreeModel)?.mainFrame;
  for (let i = 0; i < 200 && !ready(); i++) {
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  const target = SDK.TargetManager.TargetManager.instance().primaryPageTarget();
  const cssModel = target.model(SDK.CSSModel.CSSModel);
  const frameId = target.model(SDK.ResourceTreeModel.ResourceTreeModel).mainFrame.id;
  const inspectorSheets = async () => {
    const sheets = [];
    for (const header of cssModel.styleSheetHeaders()) {
      if (header.isViaInspector()) {
        sheets.push({id: header.id, text: await cssModel.getStyleSheetText(header.id)});
      }
    }
    return sheets;
  };
  globalThis.__d = {cssModel, frameId, inspectorSheets};
};

try {
  const [page] = await browser.pages();
  await page.goto(url);
  const devtools = await (await browser.waitForTarget(t => t.url().startsWith('devtools://'))).asPage();
  await devtools.waitForFunction(() => document.readyState === 'complete');
  await sleep(3000);
  await devtools.evaluate(frontendHelpers);

  let r = await devtools.evaluate(async () => {
    const {cssModel, frameId, inspectorSheets} = globalThis.__d;
    // Another feature's forced sheet, before any default exists.
    const forced = await cssModel.createInspectorStylesheet(frameId, true);
    await cssModel.setStyleSheetText(forced.id, '/* ai */\n#c { color: rgb(0, 0, 3); }', true);
    const [first, second, third] = await Promise.all([
      cssModel.requestViaInspectorStylesheet(frameId),
      cssModel.requestViaInspectorStylesheet(frameId),
      cssModel.requestViaInspectorStylesheet(frameId),
    ]);
    const sheets = await inspectorSheets();
    return {forced: forced.id, first: first?.id, same: first === second && second === third, count: sheets.length,
            firstText: await cssModel.getStyleSheetText(first.id)};
  });
  check('with a forced sheet created first, New Style Rule gets a separate, empty default',
        r.first && r.first !== r.forced && r.firstText === '', JSON.stringify(r));
  check('concurrent requests share one default, and nothing extra is created', r.same && r.count === 2, JSON.stringify(r));

  r = await devtools.evaluate(async () => {
    const {cssModel, frameId} = globalThis.__d;
    const header = await cssModel.requestViaInspectorStylesheet(frameId);
    const error = await cssModel.setStyleSheetText(header.id, '/* mine */\n#a {\n  color: rgb(1, 0, 0);\n}\n', false);
    return {ok: error === null, id: header.id};
  });
  check('a user rule lands in the default and is in effect', r.ok && await color(page, 'a') === 'rgb(1, 0, 0)' &&
        await color(page, 'c') === 'rgb(0, 0, 3)');

  await devtools.close();
  await sleep(1500);
  const pageTargetId = page.target()._targetId;
  for (let session = 1; session <= 3; session++) {
    const frontend = await browser.newPage();
    await frontend.goto(
        `http://localhost:${DEBUG_PORT}/devtools/devtools_app.html?ws=localhost:${DEBUG_PORT}/devtools/page/${pageTargetId}`);
    await sleep(5000);
    await frontend.evaluate(frontendHelpers);
    r = await frontend.evaluate(async rule => {
      const {cssModel, frameId, inspectorSheets} = globalThis.__d;
      const before = (await inspectorSheets()).length;
      const header = await cssModel.requestViaInspectorStylesheet(frameId);
      const text = await cssModel.getStyleSheetText(header.id);
      const error = await cssModel.setStyleSheetText(header.id, `${text}${rule}\n`, false);
      return {before, after: (await inspectorSheets()).length, text, ok: error === null};
    }, `#b { outline-color: rgb(0, ${session}, 0); }`);
    check(`reopened session ${session}: New Style Rule gets the same default with the user's rules`,
          r.ok && r.text.startsWith('/* mine */\n#a {\n  color: rgb(1, 0, 0);\n}\n') && r.before === 2 && r.after === 2 &&
              await color(page, 'a') === 'rgb(1, 0, 0)' &&
              await page.evaluate(() => getComputedStyle(document.getElementById('b')).outlineColor) ===
                  `rgb(0, ${session}, 0)`,
          JSON.stringify(r));
    await frontend.close();
    await sleep(1000);
  }
  check('the forced sheet is untouched through it all', await color(page, 'c') === 'rgb(0, 0, 3)');
} finally {
  server.close();
  await browser.close();
}
const failed = results.filter(result => !result.ok).length;
console.log(`\n${results.length - failed}/${results.length} checks passed`);
process.exit(failed ? 1 : 0);
