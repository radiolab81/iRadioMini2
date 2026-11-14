/* Wird im Seitenkontext ausgefuehrt (jsdom: window.eval in beforeParse, Chromium: addInitScript): ersetzt fetch durch einen simulierten
 * Server und stellt window.__run() bereit, das den Dateibrowser der echten index.html prueft. Ergebnis: [{ok, msg}, ...] */
(function () {
  const EVIL = '<img src=x onerror="window.__xss=1">';
  const big = []; for (let i = 0; i < 45; i++) big.push({ n: 't' + String(i).padStart(2, '0') + '.mp3' });
  const fsys = {
    '/': [{ n: 'Rock', d: 1 }, { n: 'Big', d: 1 }, { n: 'leer', d: 1 }, { n: 'top.mp3' }],
    '/Rock': [{ n: 'Live', d: 1 }, { n: 'a.mp3' }, { n: EVIL + '.mp3' }],
    '/Rock/Live': [{ n: 'l1.mp3' }],
    '/Big': big, '/leer': [],
  };
  const srv = { source: 'radio', path: '', count: 3, index: 0, posts: [], gets: [], playFail: false };
  window.__srv = srv;
  const reply = (o, status = 200) => Promise.resolve({ ok: status < 300, status, json: async () => o });
  window.fetch = (url, opt) => {
    const u = new URL(url, 'http://x/'), p = u.pathname;
    if (opt && opt.method === 'POST') {
      const body = JSON.parse(opt.body); srv.posts.push({ p, body });
      if (p === '/api/files/play') { if (srv.playFail) return reply({}, 404); srv.path = body.path; return reply({ ok: true }); }
      return reply({ ok: true });
    }
    srv.gets.push(u.pathname + u.search);
    if (p === '/api/status') return reply(Object.assign({ net: true, player: 'playing', name: 'T', source: srv.source, volume: 30, index: srv.index, count: srv.count, eq: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] }, srv.source === 'media' && srv.path ? { path: srv.path } : {}));
    if (p === '/api/list') return reply(['A', 'B', 'C']);
    if (p === '/api/files') {
      const path = u.searchParams.get('path'), lim = +u.searchParams.get('limit') || 30, after = u.searchParams.get('after');
      const all = fsys[path]; if (!all) return reply({}, 404);
      let start = 0; if (after) start = all.findIndex(x => (x.d ? 'd:' : 'f:') + x.n === after) + 1;
      const items = all.slice(start, start + lim);
      return reply({ path, total: all.length, more: start + lim < all.length, items });
    }
    return reply({}, 404);
  };

  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const $ = id => document.getElementById(id);
  const rows = () => [...document.querySelectorAll('#flist li')];
  const crumbs = () => [...document.querySelectorAll('#crumbs button')].map(b => b.textContent);
  const names = () => rows().map(l => (l.querySelector('.n') || l).textContent);
  const lastGet = () => srv.gets.filter(g => g.startsWith('/api/files')).pop() || '';
  const q = g => new URL(g, 'http://x').searchParams;

  window.__run = async function () {
    const res = [];
    const check = (ok, msg) => res.push({ ok: !!ok, msg });
    await sleep(300);

    check($('fb').hidden === true && $('list').hidden === false, 'Radio: Dateibrowser verborgen, flache Liste sichtbar');
    check(!srv.gets.some(g => g.startsWith('/api/files')), 'Radio: keine Dateibrowser-Anfragen');

    srv.source = 'media'; srv.path = '/Rock/a.mp3'; srv.count = 2; await window.poll(); await sleep(150);
    check($('fb').hidden === false && $('list').hidden === true, 'Dateien: Browser sichtbar, flache Liste verborgen');
    check(q(lastGet()).get('path') === '/Rock', 'Start im Ordner der laufenden Datei: ' + lastGet());
    check(JSON.stringify(crumbs()) === '["SD","Rock"]', 'Breadcrumb SD > Rock: ' + crumbs());
    check(rows().length === 4 && rows()[0].className === 'up' && rows()[0].textContent.includes('‥'), 'Zeile "‥" (eine Ebene hoeher) + 3 Eintraege');
    check(rows()[1].textContent.includes('Live') && rows()[1].textContent.includes('📁'), 'Ordner zuerst mit Ordnersymbol');
    check(rows()[2].classList.contains('cur') && !rows()[3].classList.contains('cur'), 'laufende Datei hervorgehoben');
    check($('fcount').textContent === '2 Titel in der Wiedergabeliste', 'Anzahl der Wiedergabeliste: ' + $('fcount').textContent);
    check(window.__xss === undefined && !document.querySelector('#flist img') && rows()[3].textContent.includes(EVIL), 'boesartiger Dateiname nur als Text (kein XSS)');

    rows()[1].click(); await sleep(100);
    check(q(lastGet()).get('path') === '/Rock/Live' && JSON.stringify(crumbs()) === '["SD","Rock","Live"]', 'Ordner antippen: hinein (' + crumbs() + ')');
    check(rows().length === 2 && rows()[1].textContent.includes('l1.mp3'), 'Inhalt des Unterordners');
    rows()[0].click(); await sleep(100);
    check(q(lastGet()).get('path') === '/Rock', '"‥" geht eine Ebene hoeher');
    document.querySelectorAll('#crumbs button')[0].click(); await sleep(100);
    check(q(lastGet()).get('path') === '/' && JSON.stringify(crumbs()) === '["SD"]', 'Breadcrumb "SD" springt zur Wurzel');
    check(rows().length === 4 && !rows().some(l => l.className === 'up') && JSON.stringify(names()) === '["Rock","Big","leer","top.mp3"]', 'Wurzel: keine "‥"-Zeile, Reihenfolge wie geliefert: ' + names());

    srv.posts.length = 0; rows()[3].click(); await sleep(100);
    check(srv.posts.length === 1 && srv.posts[0].p === '/api/files/play' && srv.posts[0].body.path === '/top.mp3', 'Datei antippen: POST /api/files/play {path:"/top.mp3"}');
    srv.playFail = true; rows()[3].click(); await sleep(100);
    check($('fmsg').textContent.includes('nicht abgespielt'), 'Fehler (404) wird gemeldet: ' + $('fmsg').textContent); srv.playFail = false;
    rows()[3].click(); await sleep(100);
    check($('fmsg').textContent === '', 'Meldung verschwindet bei Erfolg');

    rows()[1].click(); await sleep(100);   /* Big */
    check(rows().length === 31 && $('fcnt').textContent === '1–30 von 45' && $('fprev').disabled && !$('fnext').disabled, 'Big: Seite 1 (30 von 45), zurueck gesperrt, weiter frei');
    $('fnext').click(); await sleep(100);
    check(q(lastGet()).get('after') === 'f:t29.mp3', 'weiter: Cursor = letzter Eintrag (' + q(lastGet()).get('after') + ')');
    check(rows().length === 15 && !rows().some(l => l.className === 'up') && $('fcnt').textContent === '31–45 von 45' && $('fnext').disabled && !$('fprev').disabled, 'Seite 2: 15 Eintraege, keine "‥"-Zeile, weiter gesperrt');
    $('fprev').click(); await sleep(100);
    check(!q(lastGet()).get('after') && rows().length === 31 && $('fcnt').textContent === '1–30 von 45', 'zurueck: wieder Seite 1');
    rows()[0].click(); await sleep(100);
    check(q(lastGet()).get('path') === '/' && !q(lastGet()).get('after'), 'aus Big hoch: Wurzel, Cursor zurueckgesetzt');

    rows()[2].click(); await sleep(100);   /* leer */
    check(rows().length === 2 && rows()[1].textContent.includes('Keine Ordner') && $('fcnt').textContent === '', 'leerer Ordner: Hinweis statt Liste');

    srv.posts.length = 0; document.querySelector('#fb .row button').click(); await sleep(450);
    check(srv.posts.some(x => x.p === '/api/cmd' && x.body.cmd === 'media_all'), 'Alle Titel: POST /api/cmd media_all');

    go('/Rock/Live'); await sleep(100);
    srv.path = '/Rock/Live/l1.mp3'; await window.poll(); await sleep(50);
    check(rows().find(l => l.dataset.p === '/Rock/Live/l1.mp3').classList.contains('cur'), 'Markierung folgt der laufenden Datei beim naechsten Status');

    srv.source = 'radio'; await window.poll(); await sleep(50);
    check($('fb').hidden === true && $('list').hidden === false, 'zurueck zum Radio: Browser verborgen');
    srv.path = '/gone/x.mp3'; srv.source = 'media'; await window.poll(); await sleep(200);
    check(JSON.stringify(crumbs()) === '["SD"]' && q(lastGet()).get('path') === '/', 'Ordner existiert nicht mehr (404): Rueckfall auf Wurzel');
    return res;
  };
})();
