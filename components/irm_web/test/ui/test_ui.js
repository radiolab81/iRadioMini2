// UI-Test: laedt www/index.html in jsdom und simuliert die REST-API. Aufruf: node test_ui.js  (benoetigt: npm install jsdom)
const { JSDOM } = require('jsdom');
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, '../../www/index.html'), 'utf8');
let fails = 0;
const check = (c, m) => { console.log((c ? '  ok   ' : '  FEHLER ') + m); if (!c) fails++; };
const sleep = ms => new Promise(r => setTimeout(r, ms));

const EVIL = '<img src=x onerror="window.__xss=1">"><script>window.__xss=2</script>';
const items = [
  { i: 0, name: EVIL, place: '<b>Ort</b>', country: 'Germany', host: 'evil.example', port: 8073, users: 1, max: 4, snr: 33, lo: 0, hi: 30000000 },
  { i: 1, name: 'Zweiter Server', place: 'Boulder, CO', country: 'USA', host: 'srv2.example', port: 8074, users: 0, max: 0, snr: 0, lo: 0, hi: 0 },
];
const srv = { posts: [], gets: [], dirstate: 'idle', valid: true, status429: false, listTotal: 45, kiwi: null, source: 'radio', failList: false, name: 'Test', list: null };

function fakeFetch(url, opt) {
  const u = new URL(url, 'http://x/'); const p = u.pathname;
  const json = (o, status = 200) => Promise.resolve({ ok: status < 300, status, json: async () => o });
  if (opt && opt.method === 'POST') {
    const body = JSON.parse(opt.body); srv.posts.push({ p, body });
    if (p === '/api/dir/update') {
      if (srv.status429 && !body.force) return json({ ok: false, error: 'Mindestabstand', retry_in_s: 30 }, 429);
      srv.dirstate = 'updating'; setTimeout(() => { srv.dirstate = 'ok'; }, 1500);
      return json({ ok: true }, 202);
    }
    return json({ ok: true });
  }
  srv.gets.push(u.pathname + u.search);
  if (p === '/api/status') return json({ net: true, player: 'playing', name: srv.name, source: srv.source, volume: 30, index: 0, count: (srv.list || ['A', 'B', 'C']).length, eq: [0,0,0,0,0,0,0,0,0,0], kiwi: srv.kiwi, sample_rate: 12000, bitrate: 192000 });
  if (p === '/api/list') return json(srv.list || ['A', 'B', 'C']);
  if (p === '/api/dir') return json({ available: true, valid: srv.valid, count: 842, generation: 3, state: srv.dirstate, bytes: 51200, servers: 120, err: 0, msg: '', retry_in_s: 0 });
  if (p === '/api/dir/countries') return json([{ id: 5, name: 'Germany', n: 23 }, { id: 9, name: 'USA', n: 150 }]);
  if (p === '/api/dir/list') {
    if (srv.failList) return json({}, 400);
    const off = +u.searchParams.get('offset') || 0;
    return json({ total: srv.listTotal, offset: off, limit: 20, items: off === 0 ? items : items.slice(0, 1) });
  }
  return json({}, 404);
}

(async () => {
  let confirms = [], confirmAnswer = true, alerts = [];
  const dom = new JSDOM(html, { runScripts: 'dangerously', pretendToBeVisual: true, beforeParse(w) {
    w.fetch = fakeFetch; w.confirm = m => { confirms.push(m); return confirmAnswer; }; w.alert = m => alerts.push(m);
  } });
  const w = dom.window, d = w.document, $ = id => d.getElementById(id);
  await sleep(300);

  console.log('== Grundzustand');
  check($('src').textContent === 'Radio', 'Quelle: Radio');
  check($('kiwi').hidden === true, 'Kiwi-Bedienfeld im Radiobetrieb verborgen');
  check($('dir').hidden === true, 'Serversuche im Radiobetrieb verborgen');
  check(srv.gets.every(g => !g.startsWith('/api/dir')), 'Verzeichnis wird erst beim Oeffnen abgefragt (kein unnoetiger Traffic)');

  console.log('== Verzeichnis oeffnen (nur im Modus KiwiSDR sichtbar)');
  srv.kiwi = { freq_hz: 7055500, mode: 'usb', modes: ['am', 'usb'], rssi_dbm: -70 }; srv.source = 'kiwi'; await w.poll();
  check($('dir').hidden === false, 'Serversuche im KiwiSDR-Betrieb sichtbar');
  $('dir').open = true; $('dir').dispatchEvent(new w.Event('toggle'));
  await sleep(400);
  check(srv.gets.some(g => g === '/api/dir') && srv.gets.some(g => g === '/api/dir/countries') && srv.gets.some(g => g.startsWith('/api/dir/list')), 'info, countries und list abgefragt');
  check($('dui').hidden === false, 'Suchbereich sichtbar (Liste vorhanden)');
  check($('dinfo').textContent.includes('842 Server'), 'Info: ' + $('dinfo').textContent);
  check($('dc').options.length === 3 && $('dc').options[1].textContent === 'Germany (23)' && $('dc').options[1].value === '5', 'Laenderliste im Auswahlfeld');
  const lis = d.querySelectorAll('#dlist li');
  check(lis.length === 2, 'zwei Treffer dargestellt');
  console.log('== XSS-Absicherung (boesartiger Servername)');
  check(w.__xss === undefined, 'kein Skript ausgefuehrt');
  check(d.querySelectorAll('#dlist img, #dlist script, #dlist b b').length === 0 && lis[0].querySelector('b').textContent === EVIL, 'Name erscheint als reiner Text, keine Elemente eingeschleust');
  check(lis[0].textContent.includes('<b>Ort</b>'), 'Ort ebenfalls als Text');
  check(lis[1].textContent.includes('Boulder, CO') && lis[1].textContent.includes('0 Nutzer') && !lis[1].textContent.includes('SNR'), 'Server ohne Kanalzahl/SNR: sinnvoller Text (' + lis[1].querySelector('span').textContent + ')');
  check($('dcnt').textContent === '1–2 von 45', 'Zaehler: ' + $('dcnt').textContent);
  check($('dprev').disabled === true && $('dnext').disabled === false, 'Blaettern: zurueck gesperrt, weiter frei');

  console.log('== Suchen und Blaettern');
  $('dt').value = 'M\u00fcnchen Ost'; $('dc').value = '5'; $('dq').value = '25'; $('df').checked = true; $('ds').value = 'snr';
  srv.gets.length = 0; $('dnext').click(); await sleep(200);
  let q = srv.gets.find(g => g.startsWith('/api/dir/list'));
  const sp = new URL(q, 'http://x').searchParams;
  check(sp.get('offset') === '20' && sp.get('limit') === '20' && sp.get('text') === 'M\u00fcnchen Ost' && sp.get('country') === '5' && sp.get('snr') === '25' && sp.get('free') === '1' && sp.get('sort') === 'snr', 'Anfrage: ' + q);
  check(q.includes('M%C3%BCnchen+Ost'), 'Umlaut UTF-8-prozentkodiert');
  check($('dprev').disabled === false, 'zurueck jetzt frei');
  srv.gets.length = 0; $('dprev').click(); await sleep(200);
  check(new URL(srv.gets.find(g => g.startsWith('/api/dir/list')), 'http://x').searchParams.get('offset') === '0', 'zurueck: offset 0 (nie negativ)');
  srv.failList = true; $('dt').dispatchEvent(new w.KeyboardEvent('keydown', { key: 'Enter' })); await sleep(200);
  check($('dcnt').textContent.includes('ungültig'), 'HTTP 400 -> Hinweis: ' + $('dcnt').textContent); srv.failList = false;

  console.log('== Server hoeren');
  srv.posts.length = 0; lis[0].querySelector('button').click(); await sleep(100);
  check(srv.posts.length === 1 && srv.posts[0].p === '/api/dir/connect' && srv.posts[0].body.host === 'evil.example' && srv.posts[0].body.port === 8073, 'POST /api/dir/connect {host, port}: ' + JSON.stringify(srv.posts[0] && srv.posts[0].body));

  console.log('== Frequenzfilter nur mit bekannter Frequenz');
  srv.kiwi.freq_hz = 7055500; await w.poll();
  check($('kiwi').hidden === false && $('dfv').textContent === '7055.5', 'Kiwi-Betrieb: Frequenz im Filtertext (' + $('dfv').textContent + ')');
  $('dfq').checked = true; srv.gets.length = 0; $('dt').dispatchEvent(new w.KeyboardEvent('keydown', { key: 'Enter' })); await sleep(200);
  check(new URL(srv.gets.find(g => g.startsWith('/api/dir/list')), 'http://x').searchParams.get('freq') === '7055500', 'freq=7055500 (Hz) uebergeben');

  console.log('== Favorit');
  srv.posts.length = 0; w.fav(); await sleep(150);
  check(srv.posts.some(x => x.p === '/api/cmd' && x.body.cmd === 'kiwi_fav'), 'POST /api/cmd kiwi_fav');
  check($('kmsg').textContent.length > 0, 'Rueckmeldung angezeigt');

  console.log('== Favorit mit eigenem Namen');
  srv.name = 'Langwelle 225 kHz (AM)'; srv.kiwi = { ...srv.kiwi, favs: 2 }; srv.list = ['Fav A', 'Fav B', 'Direkt <b>x</b>'];
  await w.poll(); await sleep(50);
  check($('kn').value === 'Langwelle 225 kHz (AM)', 'Namensfeld mit dem Namen des laufenden Eintrags vorbelegt: ' + $('kn').value);
  $('kn').value = '  WWV 6150  ';
  srv.posts.length = 0; w.fav(); await sleep(150);
  let fp = srv.posts.find(x => x.p === '/api/cmd' && x.body.cmd === 'kiwi_fav');
  check(fp && fp.body.name === 'WWV 6150', 'kiwi_fav sendet den eingegebenen Namen (getrimmt): ' + JSON.stringify(fp && fp.body));
  await w.poll();
  check($('kn').value === '  WWV 6150  ', 'eingegebener Name wird vom Status-Abruf nicht ueberschrieben');
  $('kn').value = '   '; srv.posts.length = 0; w.fav(); await sleep(150);
  fp = srv.posts.find(x => x.p === '/api/cmd' && x.body.cmd === 'kiwi_fav');
  check(fp && !('name' in fp.body), 'leeres Namensfeld: kein Name gesendet (Radio nimmt den Namen des Eintrags)');
  await sleep(900);
  check(/nicht gespeichert/.test($('kmsg').textContent), 'Liste unveraendert -> Hinweis: ' + $('kmsg').textContent);
  check($('kn').value === '   ', 'bei Misserfolg bleibt das Namensfeld erhalten');
  $('kn').value = 'Neu'; srv.posts.length = 0; w.fav(); await sleep(100);
  srv.kiwi.favs = 3; srv.list = ['Fav A', 'Fav B', 'Neu', 'Direkt <b>x</b>']; await w.poll(); await sleep(900);
  check($('kmsg').textContent === 'Favorit gespeichert', 'Favorit gespeichert gemeldet: ' + $('kmsg').textContent);
  await w.poll();
  check($('kn').value === 'Langwelle 225 kHz (AM)', 'nach dem Speichern: Namensfeld wieder mit dem Eintragsnamen vorbelegt');
  srv.name = EVIL; await w.poll();
  check($('kn').value === EVIL && !d.querySelector('#kiwi img, #kiwi script') && w.__xss === undefined, 'Eintragsname im Namensfeld nur als Wert (kein XSS)');
  srv.name = 'Langwelle 225 kHz (AM)';

  console.log('== Preset loeschen');
  srv.kiwi.favs = 2; srv.list = ['Fav A', 'Fav B', 'Direkt <b>x</b>']; await w.poll(); await sleep(50);
  let li = d.querySelectorAll('#list li');
  check(li.length === 3 && li[0].querySelector('button.del') && li[1].querySelector('button.del') && !li[2].querySelector('button'), 'Loeschen-Button nur an gespeicherten Favoriten, nicht am Direkteintrag');
  check(li[2].textContent === 'Direkt <b>x</b>' && !d.querySelector('#list b'), 'Namen in der Liste nur als Text');
  confirmAnswer = false; confirms.length = 0; srv.posts.length = 0;
  li[1].querySelector('button.del').click(); await sleep(100);
  check(confirms.length === 1 && confirms[0].includes('Fav B') && srv.posts.length === 0, 'Rueckfrage nennt den Preset; Abbrechen sendet nichts: ' + confirms[0]);
  confirmAnswer = true; srv.posts.length = 0;
  li[1].querySelector('button.del').click(); await sleep(100);
  check(srv.posts.length === 1 && srv.posts[0].body.cmd === 'kiwi_del' && srv.posts[0].body.value === 1, 'POST kiwi_del mit Listenindex 1, ohne zusaetzliches goto: ' + JSON.stringify(srv.posts.map(x => x.body)));
  await sleep(900);
  check(/nicht gel/.test($('kmsg').textContent), 'Liste unveraendert -> Hinweis: ' + $('kmsg').textContent);
  srv.posts.length = 0; li[1].querySelector('button.del').click(); await sleep(100);
  srv.kiwi.favs = 1; srv.list = ['Fav A', 'Direkt <b>x</b>']; await w.poll(); await sleep(900);
  check($('kmsg').textContent === 'Preset gelöscht', 'Loeschen gemeldet: ' + $('kmsg').textContent);
  li = d.querySelectorAll('#list li');
  check(li.length === 2 && li[0].querySelector('button.del') && !li[1].querySelector('button'), 'Liste neu gezeichnet: ein Favorit + Direkteintrag');
  srv.posts.length = 0; li[1].click(); await sleep(50);
  check(srv.posts.length === 1 && srv.posts[0].body.cmd === 'goto' && srv.posts[0].body.value === 1, 'Klick auf die Zeile waehlt den Eintrag an (goto 1)');
  srv.kiwi = null; srv.source = 'radio'; srv.name = 'Test'; srv.list = null; await w.poll(); await sleep(50);
  check(d.querySelectorAll('#list li').length === 3 && !d.querySelector('#list button'), 'Radio: keine Loeschen-Buttons');
  srv.kiwi = { freq_hz: 7055500, mode: 'usb', modes: ['am', 'usb'], rssi_dbm: -70 }; srv.source = 'kiwi'; await w.poll();

  console.log('== Liste aktualisieren');
  srv.posts.length = 0; confirms.length = 0; srv.status429 = true;
  $('dupd').click(); await sleep(200);
  check(confirms.length === 2 && /Flash/.test(confirms[0]) && /30 s/.test(confirms[1]), 'zwei Rueckfragen: Flash-Hinweis, dann Mindestabstand (30 s)');
  check(srv.posts.length === 2 && srv.posts[0].body.force === false && srv.posts[1].body.force === true, 'erst ohne, dann mit force: ' + JSON.stringify(srv.posts.map(x => x.body)));
  await sleep(300);
  check($('dprog').textContent.includes('lädt') && $('dupd').disabled === true, 'Fortschritt sichtbar, Button gesperrt: ' + $('dprog').textContent);
  srv.gets.length = 0; await sleep(2600);
  check($('dupd').disabled === false && $('dprog').textContent === '', 'nach Abschluss wieder bedienbar');
  check(srv.gets.some(g => g === '/api/dir/countries') && srv.gets.some(g => g.startsWith('/api/dir/list')), 'nach Abschluss: Laender und Liste neu geladen');
  srv.posts.length = 0; confirms.length = 0; srv.status429 = false; srv.dirstate = 'idle'; confirmAnswer = false;
  $('dupd').click(); await sleep(100);
  check(srv.posts.length === 0, 'Abbrechen in der Rueckfrage sendet nichts');

  console.log('== Ergebnis: ' + (fails ? 'FEHLER (' + fails + ')' : 'OK'));
  process.exit(fails ? 1 : 0);
})();
