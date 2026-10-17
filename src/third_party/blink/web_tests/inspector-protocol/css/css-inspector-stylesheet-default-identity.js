(async function(/** @type {import('test_runner').TestRunner} */ testRunner) {
  let {page, session, dp} = await testRunner.startHTML(`
    <div id="a">a</div><div id="b">b</div>
  `, 'Tests that CSS.createStyleSheet without force returns the same default inspector stylesheet across reattaches, whatever forced sheets exist.');

  await dp.Page.enable();
  const frameId = (await dp.Page.getResourceTree()).result.frameTree.frame.id;
  await dp.DOM.enable();
  await dp.CSS.enable();

  const text = async (dp, styleSheetId) => (await dp.CSS.getStyleSheetText({styleSheetId})).result.text;
  const colors = async session => {
    const values = [];
    for (const id of ['a', 'b']) {
      values.push(`${id}=${await session.evaluate(`getComputedStyle(document.getElementById('${id}')).color`)}`);
    }
    return values.join(' ');
  };

  // A forced sheet (another feature's) exists before the default.
  const forced = (await dp.CSS.createStyleSheet({frameId, force: true})).result.styleSheetId;
  await dp.CSS.setStyleSheetText({styleSheetId: forced, text: '/* studio */\n'});
  const defaultId = (await dp.CSS.createStyleSheet({frameId})).result.styleSheetId;
  testRunner.log('Default differs from the forced sheet: ' + (defaultId !== forced));
  const userText = '/* mine */\n#a {\n  color: rgb(1, 0, 0);\n}\n';
  await dp.CSS.setStyleSheetText({styleSheetId: defaultId, text: userText});
  testRunner.log('Default is reused in the same session: ' +
      ((await dp.CSS.createStyleSheet({frameId})).result.styleSheetId === defaultId));

  for (let reattach = 1; reattach <= 3; reattach++) {
    await session.disconnect();
    session = await page.createSession();
    dp = session.protocol;
    let inspectorSheets = 0;
    dp.CSS.onStyleSheetAdded(event => {
      if (event.params.header.origin === 'inspector') {
        inspectorSheets++;
      }
    });
    await dp.DOM.enable();
    await dp.CSS.enable();
    const reported = inspectorSheets;
    const id = (await dp.CSS.createStyleSheet({frameId})).result.styleSheetId;
    testRunner.log(`Reattach ${reattach}: default keeps its text: ${(await text(dp, id)) === userText}; ` +
        `inspector sheets ${reported}, new ones created: ${inspectorSheets - reported}`);
  }

  // The Styles pane's "New Style Rule": ask for the default, append to it.
  const id = (await dp.CSS.createStyleSheet({frameId})).result.styleSheetId;
  await dp.CSS.setStyleSheetText({styleSheetId: id, text: (await text(dp, id)) + '#b { color: rgb(0, 2, 0); }\n'});
  testRunner.log('Default after a new rule: ' + JSON.stringify(await text(dp, id)));
  testRunner.log('Colors: ' + await colors(session));

  testRunner.log('\nReloading');
  await dp.Page.enable();
  dp.Page.reload();
  await dp.Page.onceLoadEventFired();
  const afterReload = (await dp.CSS.createStyleSheet({frameId})).result.styleSheetId;
  testRunner.log('Default in the new document: ' + JSON.stringify(await text(dp, afterReload)));

  testRunner.completeTest();
})
