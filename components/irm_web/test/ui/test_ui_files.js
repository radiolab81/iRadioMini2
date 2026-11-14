// UI-Test Dateibrowser in jsdom: node test_ui_files.js   (benoetigt: npm install jsdom). Gleiche Pruefungen wie test_ui_files_pw.js (Chromium).
const { JSDOM } = require('jsdom');
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, '../../www/index.html'), 'utf8');
const inpage = fs.readFileSync(path.join(__dirname, 'ui_files_inpage.js'), 'utf8');
(async () => {
  const dom = new JSDOM(html, { runScripts: 'dangerously', pretendToBeVisual: true, beforeParse(w) { w.eval(inpage); } });
  const res = await dom.window.eval('__run()');
  let fails = 0;
  for (const r of res) { console.log((r.ok ? '  ok   ' : '  FEHLER ') + r.msg); if (!r.ok) fails++; }
  console.log('== Ergebnis: ' + (fails ? 'FEHLER (' + fails + ')' : 'OK'));
  process.exit(fails ? 1 : 0);
})();
