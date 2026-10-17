// Raw CDP verification of the canonical default inspector stylesheet.
//
// The Styles pane's "New Style Rule" asks for the frame's default inspector
// stylesheet with CSS.createStyleSheet(force: false). It must get the same
// default whatever forced inspector stylesheets (Style Studio, upstream AI
// assistance) exist, in whatever order, in every DevTools session. Each CDP
// session here is what a DevTools session is to Blink. Checks use stylesheet
// text, inspector-sheet counts, and computed styles in the page.
//
// Usage (from the repo root, after `npm run build`):
//   chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
//     docs/style-intelligence/fixtures/measure-inspector-stylesheet-default.mjs [out-dir]
// Exits non-zero if any check fails.
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));  // docs/style-intelligence/fixtures
const ROOT = path.resolve(HERE, '../../..');
const {default: puppeteer} = await import(path.join(
    ROOT, 'chromium/src/third_party/devtools-frontend/src/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const out = path.resolve(process.argv[2] ?? 'inspector-stylesheet-default-out');
fs.mkdirSync(out, {recursive: true});

const PAGE = `<!doctype html><meta charset="utf-8"><title>default inspector stylesheet</title>
<style>body { color: rgb(0, 0, 0); }</style>
<div id="a">a</div><div id="b">b</div><div id="c">c</div>
<iframe id="frame" srcdoc="<p id='p'>p</p>"></iframe>`;
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

const USER = '/* mine */\n#a {\n  color: rgb(1, 0, 0);\n}\n';

async function connect(page) {
  const session = await page.target().createCDPSession();
  const inspector = [];
  session.on('CSS.styleSheetAdded', ({header}) => {
    if (header.origin === 'inspector') {
      inspector.push(header.styleSheetId);
    }
  });
  session.on('CSS.styleSheetRemoved', ({styleSheetId}) => {
    const index = inspector.indexOf(styleSheetId);
    if (index >= 0) {
      inspector.splice(index, 1);
    }
  });
  await session.send('DOM.enable');
  await session.send('CSS.enable');
  const {frameTree} = await session.send('Page.getFrameTree');
  const create = async (force, frameId = frameTree.frame.id) =>
      (await session.send('CSS.createStyleSheet', {frameId, force})).styleSheetId;
  const text = async id => (await session.send('CSS.getStyleSheetText', {styleSheetId: id})).text;
  const setText = (id, value) => session.send('CSS.setStyleSheetText', {styleSheetId: id, text: value});
  return {
    session, inspector, create, text, setText, frameTree,
    // What "New Style Rule" now asks for.
    defaultSheet: () => create(false),
    forced: async label => {
      const id = await create(true);
      await setText(id, `/* ${label} */\n`);
      return id;
    },
  };
}
const color = (page, id) => page.evaluate(i => getComputedStyle(document.getElementById(i)).color, id);

async function freshPage() {
  const page = await browser.newPage();
  await page.goto(url);
  return page;
}

try {
  // 1-4: creation orders. The default is told apart by its own text.
  for (const [name, plan] of [
           ['1. default created before forced sheets', ['default', 'studio']],
           ['2. forced sheet created before the default', ['studio', 'default']],
           ['3. several forced sheets before the default', ['ai-1', 'ai-2', 'studio', 'default']],
           ['4. several forced sheets after the default', ['default', 'studio-1', 'studio-2', 'studio-3']],
  ]) {
    const page = await freshPage();
    const s = await connect(page);
    for (const label of plan) {
      if (label === 'default') {
        await s.setText(await s.defaultSheet(), USER);
      } else {
        await s.forced(label);
      }
    }
    const chosen = await s.text(await s.defaultSheet());
    check(name, chosen === USER, JSON.stringify(chosen));
    await s.session.detach();
    await page.close();
  }

  // 5-10, 13: one page through reconnects.
  {
    const page = await freshPage();
    const s1 = await connect(page);
    const studio = await s1.forced('studio');
    await s1.setText(studio, '#c { color: rgb(0, 0, 3); }');
    const defaultId = await s1.defaultSheet();
    await s1.setText(defaultId, USER);
    check('5. the default is reused within one session', await s1.defaultSheet() === defaultId &&
          s1.inspector.length === 2);
    await s1.session.detach();

    let survived = true;
    let noneCreated = true;
    for (let i = 1; i <= 3; i++) {
      const s = await connect(page);
      const before = s.inspector.length;
      const id = await s.defaultSheet();
      survived = survived && await s.text(id) === USER && await color(page, 'a') === 'rgb(1, 0, 0)';
      noneCreated = noneCreated && s.inspector.length === before && before === 2;
      // Abrupt: detach without CSS.disable.
      await s.session.detach();
    }
    check('6. the same default comes back across three reconnects, with nothing new created', noneCreated);
    check('7. user rules survive reconnects, comments and formatting included', survived);
    check('13. abrupt disconnects do not change which sheet is the default', survived && noneCreated);

    const s2 = await connect(page);
    const id = await s2.defaultSheet();
    await s2.setText(id, `${await s2.text(id)}#b { color: rgb(0, 2, 0); }\n`);
    check('8. a new Styles-pane rule appends to the canonical default',
          await s2.text(id) === `${USER}#b { color: rgb(0, 2, 0); }\n` && await color(page, 'b') === 'rgb(0, 2, 0)' &&
              await color(page, 'a') === 'rgb(1, 0, 0)');
    const texts = await Promise.all(s2.inspector.map(sheet => s2.text(sheet)));
    const studioAgain = s2.inspector[texts.indexOf('#c { color: rgb(0, 0, 3); }')];
    check('9. forced sheets keep their own text', studioAgain !== undefined && await color(page, 'c') === 'rgb(0, 0, 3)',
          JSON.stringify(texts));
    await s2.setText(studioAgain, '');
    check('10. clearing Studio\'s sheet leaves the user rules', await s2.text(id) === `${USER}#b { color: rgb(0, 2, 0); }\n` &&
          await color(page, 'c') === 'rgb(0, 0, 0)' && await color(page, 'a') === 'rgb(1, 0, 0)' &&
              await color(page, 'b') === 'rgb(0, 2, 0)');

    // 11. Main-frame navigation: a new document, a new default.
    await page.reload();
    const s3 = await connect(page);
    const fresh = await s3.defaultSheet();
    check('11. after a main-frame navigation the new document gets a new, empty default',
          await s3.text(fresh) === '' && await color(page, 'a') === 'rgb(0, 0, 0)' && s3.inspector.length === 1);

    // 12. Frame destruction: the frame's default goes with it; the main one stays.
    const childFrame = s3.frameTree.childFrames?.[0]?.frame.id;
    const childDefault = childFrame ? await s3.create(false, childFrame) : null;
    await s3.setText(fresh, USER);
    await page.evaluate(() => document.getElementById('frame').remove());
    await new Promise(resolve => setTimeout(resolve, 300));
    let goneError = null;
    try {
      await s3.create(false, childFrame);
    } catch (error) {
      goneError = error.message;
    }
    check('12. a destroyed frame\'s default is released and the main default is untouched',
          childDefault !== null && goneError !== null && !s3.inspector.includes(childDefault) &&
              await s3.defaultSheet() === fresh && await s3.text(fresh) === USER,
          `${goneError}`);
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
