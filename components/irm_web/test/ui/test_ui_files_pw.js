// UI-Test Dateibrowser in echtem Chromium: node test_ui_files_pw.js   (benoetigt: playwright mit Chromium; PLAYWRIGHT_PATH optional)
const fs = require('fs'), path = require('path');
const { chromium } = require(process.env.PLAYWRIGHT_PATH || 'playwright');
const html = fs.readFileSync(path.join(__dirname, '../../www/index.html'), 'utf8');
const inpage = fs.readFileSync(path.join(__dirname, 'ui_files_inpage.js'), 'utf8');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  page.on('pageerror', e => console.log('  Seitenfehler: ' + e.message));
  await page.addInitScript(inpage);
  await page.route('http://t/', r => r.fulfill({ contentType: 'text/html; charset=utf-8', body: html }));
  await page.goto('http://t/');
  const res = await page.evaluate(() => window.__run());
  let fails = 0;
  for (const r of res) { console.log((r.ok ? '  ok   ' : '  FEHLER ') + r.msg); if (!r.ok) fails++; }
  console.log('== Ergebnis: ' + (fails ? 'FEHLER (' + fails + ')' : 'OK'));
  await browser.close();
  process.exit(fails ? 1 : 0);
})();
