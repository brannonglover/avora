(async function(/** @type {import('test_runner').TestRunner} */ testRunner) {
  let {page, session, dp} = await testRunner.startHTML(`
    <div id="a">a</div><div id="b">b</div><div id="c">c</div>
  `, 'Tests that inspector stylesheets of one document keep their own text, including across a reattach.');

  await dp.Page.enable();
  const frameId = (await dp.Page.getResourceTree()).result.frameTree.frame.id;
  await dp.DOM.enable();
  await dp.CSS.enable();

  const text = async (dp, styleSheetId) => (await dp.CSS.getStyleSheetText({styleSheetId})).result.text;
  const setText = (dp, styleSheetId, text) => dp.CSS.setStyleSheetText({styleSheetId, text});
  const colors = async session => {
    const values = [];
    for (const id of ['a', 'b', 'c']) {
      values.push(`${id}=${await session.evaluate(`getComputedStyle(document.getElementById('${id}')).color`)}`);
    }
    return values.join(' ');
  };

  const userId = (await dp.CSS.createStyleSheet({frameId})).result.styleSheetId;
  await setText(dp, userId, '#a { color: rgb(1, 0, 0); }');
  const secondId = (await dp.CSS.createStyleSheet({frameId, force: true})).result.styleSheetId;
  testRunner.log('Text of a new second inspector sheet: ' + JSON.stringify(await text(dp, secondId)));
  await setText(dp, secondId, '#b { color: rgb(0, 2, 0); }');
  testRunner.log('User sheet: ' + JSON.stringify(await text(dp, userId)));
  testRunner.log('Second sheet: ' + JSON.stringify(await text(dp, secondId)));
  testRunner.log('Colors: ' + await colors(session));
  // The second sheet is written last. Its text must not become the user's.
  await setText(dp, secondId, '/* marker */\n');

  testRunner.log('\nDisconnecting and reattaching');
  await session.disconnect();
  session = await page.createSession();
  dp = session.protocol;
  const inspectorSheets = [];
  dp.CSS.onStyleSheetAdded(event => {
    if (event.params.header.origin === 'inspector') {
      inspectorSheets.push(event.params.header.styleSheetId);
    }
  });
  await dp.DOM.enable();
  await dp.CSS.enable();
  const texts = [];
  for (const id of inspectorSheets) {
    texts.push(await text(dp, id));
  }
  // Reattach reports sheets in no particular order; sort for a stable baseline.
  testRunner.log('Inspector sheet texts after reattach: ' + JSON.stringify([...texts].sort()));

  // "New Style Rule" in the user's sheet.
  const user = inspectorSheets[texts.indexOf('#a { color: rgb(1, 0, 0); }')];
  const second = inspectorSheets[texts.indexOf('/* marker */\n')];
  await setText(dp, user, (await text(dp, user)) + '\n#c { color: rgb(0, 0, 3); }');
  testRunner.log('User sheet after adding a rule: ' + JSON.stringify(await text(dp, user)));
  testRunner.log('Colors: ' + await colors(session));

  await setText(dp, second, '');
  testRunner.log('User sheet after clearing the second sheet: ' + JSON.stringify(await text(dp, user)));
  testRunner.log('Colors: ' + await colors(session));

  const third = (await dp.CSS.createStyleSheet({frameId, force: true})).result.styleSheetId;
  testRunner.log('Text of a sheet created after reattach: ' + JSON.stringify(await text(dp, third)));

  testRunner.completeTest();
})
