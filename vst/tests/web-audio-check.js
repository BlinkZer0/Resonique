// Plays a note through the web build's real audio path: serves web/index.html over HTTP (so the
// AudioWorklet is allowed), opens it in headless Edge / Chrome with autoplay permitted, and reads the
// result of the page's ?selftest hook (see webhost/web-host.js) over the DevTools protocol.
// Prints the result as JSON; exit code 0 when a note sounded through the AudioWorklet.
// Usage: node vst/tests/web-audio-check.js [web/index.html]
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');

const page = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'index.html'));
const browsers = [
  path.join(process.env['ProgramFiles(x86)'] || '', 'Microsoft/Edge/Application/msedge.exe'),
  path.join(process.env.ProgramFiles || '', 'Microsoft/Edge/Application/msedge.exe'),
  path.join(process.env.LOCALAPPDATA || '', 'Google/Chrome/Application/chrome.exe'),
  path.join(process.env.ProgramFiles || '', 'Google/Chrome/Application/chrome.exe'),
].filter(p => fs.existsSync(p));
if (!browsers.length) { console.error('FAIL: Edge or Chrome not found'); process.exit(2); }

const html = fs.readFileSync(page);
const server = http.createServer((req, res) => { res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); res.end(html); });
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const fetchJson = (url) => new Promise((resolve, reject) => http.get(url, (res) => { let s = ''; res.on('data', d => s += d); res.on('end', () => { try { resolve(JSON.parse(s)); } catch (e) { reject(e); } }); }).on('error', reject));

(async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const url = `http://127.0.0.1:${server.address().port}/index.html?selftest=1`;
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'resonique-web-'));
  const browser = spawn(browsers[0], ['--headless=new', '--disable-gpu', '--no-first-run', '--autoplay-policy=no-user-gesture-required',
    '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank'], { stdio: 'ignore' });
  let result = null, error = '';
  try {
    // the browser writes its DevTools port into the profile folder
    const portFile = path.join(profile, 'DevToolsActivePort');
    for (let i = 0; i < 100 && !fs.existsSync(portFile); i++) await sleep(100);
    if (!fs.existsSync(portFile)) throw new Error('browser did not start (no DevToolsActivePort)');
    const port = parseInt(fs.readFileSync(portFile, 'utf8').split('\n')[0], 10);
    const targets = await fetchJson(`http://127.0.0.1:${port}/json`);
    const target = targets.find(t => t.type === 'page');
    if (!target) throw new Error('no page target');
    const ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('websocket failed')); });
    let id = 0;
    const pending = new Map();
    ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
    const send = (method, params) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params: params || {} })); });
    await send('Page.navigate', { url });
    const t0 = Date.now();
    while (Date.now() - t0 < 25000) {
      await sleep(500);
      const r = await send('Runtime.evaluate', { expression: "(document.getElementById('rq-selftest') || {}).textContent || ''", returnByValue: true });
      const text = r.result && r.result.result ? r.result.result.value : '';
      if (text) { result = JSON.parse(text); break; }
    }
    if (!result) error = 'the self-test did not report within 25 s';
    ws.close();
  } catch (e) { error = String(e && e.message || e); }
  finally {
    browser.kill();
    server.close();
    await sleep(300);
    try { fs.rmSync(profile, { recursive: true, force: true }); } catch (e) { /* the browser may still hold a file */ }
  }
  if (!result) { console.log(JSON.stringify({ ok: false, error })); process.exit(1); }
  console.log(JSON.stringify(result));
  process.exit(result.ok && result.mode === 'AudioWorklet' ? 0 : 1);
})();
