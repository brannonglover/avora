// Measures whether a frontend-persisted ledger could identify Style Studio's
// own inspector stylesheets after DevTools reconnects (Checkpoint 2A.3).
//
// Nothing here sweeps. The script plays the part of a future Studio: it
// records a ledger entry when it creates a forced inspector stylesheet, then
// checks, in later CDP sessions (each one is what a DevTools session is to
// Blink), whether the entry still identifies exactly that sheet, across
// reconnects, abrupt disconnects, navigation, renderer replacement, and frame
// removal. Cleanup happens only in the "user confirmed" step, and the entry is
// dropped only after the cleanup is read back and seen in computed style.
//
// Usage (from the repo root, after `npm run build`):
//   chromium/src/third_party/node/mac_arm64/node-darwin-arm64/bin/node \
//     docs/style-intelligence/fixtures/measure-studio-orphan-ledger.mjs [out-dir]
// Exits non-zero if any check fails.
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const {default: puppeteer} = await import(path.join(
    ROOT, 'chromium/src/third_party/devtools-frontend/src/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const out = path.resolve(process.argv[2] ?? 'studio-orphan-ledger-out');
fs.mkdirSync(out, {recursive: true});

const PAGE = `<!doctype html><meta charset="utf-8"><title>orphan ledger</title>
<style>body { color: rgb(0, 0, 0); }</style>
<div id="a">a</div><div id="b">b</div>
<iframe id="frame" srcdoc="<p id='p'>p</p>"></iframe>`;
const server = http.createServer((request, response) => {
  response.writeHead(200, {'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store'});
  response.end(PAGE);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const port = server.address().port;
// Two sites on one server: localhost and 127.0.0.1 are different sites, so
// moving between them swaps the renderer process under site isolation.
const urlA = `http://localhost:${port}/`;
const urlB = `http://127.0.0.1:${port}/`;

const results = [];
function check(name, ok, detail = '') {
  results.push({name, ok});
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  — ${detail}` : ''}`);
}
function note(text) {
  console.log(`NOTE  ${text}`);
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

const browser = await puppeteer.launch({
  executablePath: path.join(ROOT, 'chromium/src/out/Default/Avora.app/Contents/MacOS/Avora'),
  headless: false,
  defaultViewport: null,
  userDataDir: path.join(out, 'profile'),
  args: ['--no-first-run', '--no-default-browser-check'],
});

const MARKER = '/* avora-style-studio-preview v1 */\n';

/** One DevTools session, to Blink: DOM and CSS enabled, inspector headers tracked. */
async function connect(page) {
  const session = await page.target().createCDPSession();
  const headers = new Map();
  session.on('CSS.styleSheetAdded', ({header}) => headers.set(header.styleSheetId, header));
  session.on('CSS.styleSheetRemoved', ({styleSheetId}) => headers.delete(styleSheetId));
  await session.send('DOM.enable');
  await session.send('CSS.enable');
  const {frameTree} = await session.send('Page.getFrameTree');
  const {root} = await session.send('DOM.getDocument', {depth: -1, pierce: true});
  const {targetInfo} = await session.send('Target.getTargetInfo');
  const childDocument = root.children?.find(n => n.localName === 'html')
                            ?.children?.find(n => n.localName === 'body')
                            ?.children?.find(n => n.localName === 'iframe')?.contentDocument;
  return {
    session, headers, frameTree, root, childDocument, targetId: targetInfo.targetId,
    text: async id => (await session.send('CSS.getStyleSheetText', {styleSheetId: id})).text,
    setText: (id, text) => session.send('CSS.setStyleSheetText', {styleSheetId: id, text}),
    create: async (frameId, force) => (await session.send('CSS.createStyleSheet', {frameId, force})).styleSheetId,
  };
}

/** What a future Studio would persist when it creates its sheet. */
function ledgerEntry(s, sheetId, frameId, documentBackendNodeId, text) {
  return {targetId: s.targetId, frameId, sheetId, documentBackendNodeId, text};
}

/**
 * The proposed recovery test: the recorded id is reported, as an inspector
 * sheet of the recorded frame, owned by the recorded document, and still
 * holding exactly what Studio last wrote. Returns why not, or null.
 */
async function candidateProblem(s, entry) {
  if (s.targetId !== entry.targetId) {
    return 'different target';
  }
  const header = s.headers.get(entry.sheetId);
  if (!header) {
    return 'not reported';
  }
  if (header.origin !== 'inspector') {
    return 'not an inspector sheet';
  }
  if (header.frameId !== entry.frameId) {
    return 'different frame';
  }
  if (header.ownerNode !== entry.documentBackendNodeId) {
    return 'different owner document';
  }
  if (await s.text(entry.sheetId) !== entry.text) {
    return 'text changed since Studio wrote it';
  }
  return null;
}

const color = (page, id) => page.evaluate(i => getComputedStyle(document.getElementById(i)).color, id);

try {
  const page = await browser.newPage();
  await page.goto(urlA);

  // --- Studio creates its sheet and records it. ---
  let s = await connect(page);
  const frameId = s.frameTree.frame.id;
  const userDefault = await s.create(frameId, false);
  await s.setText(userDefault, '/* mine */\n#b { color: rgb(0, 0, 9); }\n');
  const studio = await s.create(frameId, true);
  const previewText = `${MARKER}#a { color: rgb(200, 0, 0); }\n`;
  await s.setText(studio, previewText);
  // Another feature's forced sheet holding the very same text: text alone can never identify Studio's.
  const lookalike = await s.create(frameId, true);
  await s.setText(lookalike, previewText.replace('#a', '#b'));
  const entry = ledgerEntry(s, studio, frameId, s.root.backendNodeId, previewText);
  const studioHeader = s.headers.get(studio);
  check('a forced sheet\'s header names its document as ownerNode, and its frame',
        studioHeader?.ownerNode === s.root.backendNodeId && studioHeader?.frameId === frameId &&
            studioHeader?.origin === 'inspector',
        JSON.stringify({ownerNode: studioHeader?.ownerNode, document: s.root.backendNodeId}));
  note(`sheet ids look like ${studio}: "style-sheet-<unique process id>-<per-process counter>"`);
  await s.session.detach();  // Abrupt: no CSS.disable, nothing released.

  // --- Normal and abrupt reconnects. ---
  let stable = true;
  let unchanged = true;
  for (let i = 0; i < 3; i++) {
    s = await connect(page);
    stable = stable && await candidateProblem(s, entry) === null;
    unchanged = unchanged && s.headers.has(lookalike) && s.headers.has(userDefault) && s.targetId === entry.targetId;
    if (i % 2) {
      await s.session.send('CSS.disable');
    }
    await s.session.detach();
  }
  check('across three reconnects (abrupt and clean), the ledger entry still identifies exactly Studio\'s sheet', stable);
  check('the page target id and every other sheet id are unchanged across reconnects', unchanged);

  // --- Another client changed Studio's sheet while DevTools was away. ---
  s = await connect(page);
  const outsider = `${previewText}#b { outline-color: rgb(0, 9, 0); }\n`;
  await s.setText(studio, outsider);
  check('a sheet changed by someone else since Studio wrote it is not a candidate',
        await candidateProblem(s, entry) === 'text changed since Studio wrote it');
  await s.setText(studio, previewText);
  check('the lookalike forced sheet, with Studio-like text, never matches Studio\'s entry',
        await candidateProblem(s, {...entry, sheetId: lookalike}) === 'text changed since Studio wrote it' &&
            await candidateProblem(s, {...entry, sheetId: lookalike, text: await s.text(lookalike)}) === null,
        'identity rests on the recorded id; the text check only confirms');
  await s.session.detach();

  // --- User-confirmed cleanup, confirmed before the entry is dropped. ---
  s = await connect(page);
  let ledger = [entry];
  if (await candidateProblem(s, entry) === null) {
    await s.setText(entry.sheetId, MARKER);
    const readBack = await s.text(entry.sheetId);
    const restored = await color(page, 'a') === 'rgb(0, 0, 0)';
    if (readBack === MARKER && restored) {
      ledger = ledger.filter(e => e !== entry);
    }
  }
  check('user-confirmed cleanup is read back and seen in computed style before the entry is dropped',
        // #b keeps the lookalike sheet's color, which the cleanup must not touch.
        ledger.length === 0 && await color(page, 'a') === 'rgb(0, 0, 0)' && await color(page, 'b') === 'rgb(200, 0, 0)' &&
            await s.text(userDefault) === '/* mine */\n#b { color: rgb(0, 0, 9); }\n');
  // A cleanup that cannot be confirmed keeps the entry.
  const entry2 = ledgerEntry(s, await s.create(frameId, true), frameId, s.root.backendNodeId, '');
  await s.setText(entry2.sheetId, previewText);
  entry2.text = previewText;
  ledger = [entry2];
  await page.evaluate(() => {
    document.getElementById('a').remove();
  });
  await s.setText(entry2.sheetId, MARKER);
  const confirmed = await s.text(entry2.sheetId) === MARKER;
  check('cleanup is confirmed by read-back when the styled element is gone; the entry may then be dropped', confirmed);
  await s.session.detach();

  // --- Same-site navigation: new document, same frame id. ---
  s = await connect(page);
  const beforeReload = {sheet: entry2.sheetId, doc: s.root.backendNodeId};
  await page.reload();
  s.session.detach().catch(() => {});
  s = await connect(page);
  const afterReload = {frame: s.frameTree.frame.id, doc: s.root.backendNodeId, ids: [...s.headers.keys()]};
  check('after a reload the main frame id is unchanged but the document backend node id is new',
        afterReload.frame === frameId && afterReload.doc !== beforeReload.doc, JSON.stringify({beforeReload, afterReload}));
  check('after a reload the recorded sheet is not reported, so its entry is stale and nothing is written',
        await candidateProblem(s, entry2) === 'not reported');
  const freshIds = [];
  for (let i = 0; i < 20; i++) {
    freshIds.push(await s.create(frameId, true));
  }
  check('sheet ids are never reissued in the same process after their sheets are gone',
        !freshIds.includes(entry.sheetId) && !freshIds.includes(entry2.sheetId), freshIds.slice(0, 3).join(', '));
  const sameProcess = freshIds[0].split('-').slice(0, 3).join('-') === entry.sheetId.split('-').slice(0, 3).join('-');
  note(`same-site reload kept the renderer process: ${sameProcess}`);
  const entry3 = ledgerEntry(s, freshIds[0], frameId, s.root.backendNodeId, previewText);
  await s.setText(entry3.sheetId, previewText);
  await s.session.detach();

  // --- Cross-site navigation: renderer process replaced. ---
  await page.goto(urlB);
  s = await connect(page);
  const crossIds = [];
  for (let i = 0; i < 3; i++) {
    crossIds.push(await s.create(s.frameTree.frame.id, true));
  }
  const prefix = id => id.split('-').slice(0, 3).join('-');
  check('after a cross-site navigation the recorded sheet is gone and new ids carry a new process prefix',
        await candidateProblem(s, entry3) === 'not reported' && prefix(crossIds[0]) !== prefix(entry3.sheetId),
        `${entry3.sheetId} → ${crossIds[0]}`);
  note(`frame id across the process swap: ${s.frameTree.frame.id === frameId ? 'unchanged' : 'changed'}; ` +
       `target id: ${s.targetId === entry.targetId ? 'unchanged' : 'changed'}; document backend node id ` +
       `${s.root.backendNodeId} (was ${entry3.documentBackendNodeId})`);
  if (s.root.backendNodeId === entry3.documentBackendNodeId) {
    note('the new process reissued the same document backend node id: owner-document ids are not unique across processes');
  }
  const entry4 = ledgerEntry(s, crossIds[0], s.frameTree.frame.id, s.root.backendNodeId, previewText);
  await s.setText(entry4.sheetId, previewText);
  await s.session.detach();

  // --- Renderer crash: process killed, page reloaded. ---
  s = await connect(page);
  await s.session.send('Page.crash').catch(() => {});
  await sleep(1000);
  await page.reload().catch(() => {});
  await sleep(1000);
  s = await connect(page);
  const afterCrash = await s.create(s.frameTree.frame.id, true);
  check('after a renderer crash the recorded sheet is gone and new ids carry a new process prefix',
        await candidateProblem(s, entry4) === 'not reported' && prefix(afterCrash) !== prefix(entry4.sheetId),
        `${entry4.sheetId} → ${afterCrash}`);
  await s.session.detach();

  // --- Frame removal. ---
  await page.goto(urlB);
  s = await connect(page);
  const childFrameId = s.frameTree.childFrames?.[0]?.frame.id;
  const childSheet = await s.create(childFrameId, true);
  await s.setText(childSheet, previewText);
  const childEntry = ledgerEntry(s, childSheet, childFrameId, s.childDocument?.backendNodeId, previewText);
  check('a child frame\'s sheet names the child document as ownerNode', await candidateProblem(s, childEntry) === null,
        JSON.stringify({owner: s.headers.get(childSheet)?.ownerNode, doc: childEntry.documentBackendNodeId}));
  await page.evaluate(() => document.getElementById('frame').remove());
  await sleep(500);
  check('removing the frame removes its sheet; the entry is stale', await candidateProblem(s, childEntry) === 'not reported');
  await s.session.detach();
} finally {
  server.close();
  await browser.close();
}
const failed = results.filter(result => !result.ok).length;
console.log(`\n${results.length - failed}/${results.length} checks passed`);
process.exit(failed ? 1 : 0);
