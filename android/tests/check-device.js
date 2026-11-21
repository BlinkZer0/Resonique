// On-device check of the Android app: installs the DEBUG apk on the connected device (adb),
// opens it, attaches to the WebView over the DevTools protocol (debug builds allow it) and
// reads the page's state: layout (compact / portrait), audio mode, and that a note sounds.
// Usage: node android/tests/check-device.js        (after gradlew assembleDebug)
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const { execFileSync } = require('child_process');

const sdk = process.env.ANDROID_HOME || path.join(process.env.LOCALAPPDATA || '', 'Android', 'Sdk');
const adb = path.join(sdk, 'platform-tools', 'adb.exe');
const apk = path.join(__dirname, '..', 'app', 'build', 'outputs', 'apk', 'debug', 'app-debug.apk');
const pkg = 'com.blinkzer0.resonique';
const run = (...args) => execFileSync(adb, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const fetchJson = (url) => new Promise((resolve, reject) => http.get(url, (res) => { let s = ''; res.on('data', d => s += d); res.on('end', () => { try { resolve(JSON.parse(s)); } catch (e) { reject(e); } }); }).on('error', reject));
let fails = 0;
const check = (ok, text) => { console.log((ok ? 'ok   ' : 'FAIL ') + text); if (!ok) fails++; };

(async () => {
  if (!fs.existsSync(apk)) { console.error('FAIL: no debug apk; run gradlew assembleDebug'); process.exit(2); }
  const devices = run('devices').split('\n').slice(1).filter(l => l.endsWith('\tdevice'));
  if (!devices.length) { console.error('FAIL: no device connected'); process.exit(2); }
  const model = run('shell', 'getprop', 'ro.product.model'), release = run('shell', 'getprop', 'ro.build.version.release');
  console.log(`device: ${model}, Android ${release}`);
  // a release build (another signature) may be installed: replace it
  try { if (run('shell', 'pm', 'list', 'packages', pkg).includes(pkg)) run('uninstall', pkg); } catch (e) { /* not installed */ }
  check(run('install', '-r', apk).includes('Success'), 'debug apk installed');
  run('shell', 'am', 'force-stop', pkg);
  run('shell', 'am', 'start', '-n', `${pkg}/.MainActivity`);
  let pid = '';
  for (let i = 0; i < 50 && !pid; i++) { await sleep(200); try { pid = run('shell', 'pidof', pkg); } catch (e) { /* not yet */ } }
  check(!!pid, `app running (pid ${pid})`);
  await sleep(2500);
  const sockets = run('shell', 'cat', '/proc/net/unix');
  const m = sockets.match(/@webview_devtools_remote_(\d+)/g) || [];
  const sock = m.find(s => s.endsWith('_' + pid)) || m[0];
  check(!!sock, `WebView DevTools socket ${sock || 'not found'}`);
  if (!sock) process.exit(1);
  run('forward', 'tcp:9229', 'localabstract:' + sock.slice(1));
  try {
    let targets = [];
    for (let i = 0; i < 20 && !targets.length; i++) { await sleep(300); try { targets = (await fetchJson('http://127.0.0.1:9229/json')).filter(t => t.type === 'page'); } catch (e) { /* retry */ } }
    check(targets.length > 0, `page target: ${targets[0] ? targets[0].url : 'none'}`);
    const ws = new WebSocket(targets[0].webSocketDebuggerUrl);
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('websocket failed')); });
    let id = 0; const pending = new Map();
    ws.onmessage = (ev) => { const msg = JSON.parse(ev.data); if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); } };
    const evaluate = (expression) => new Promise((res) => { const i = ++id; pending.set(i, (r) => res(r.result && r.result.result ? r.result.result.value : undefined)); ws.send(JSON.stringify({ id: i, method: 'Runtime.evaluate', params: { expression, returnByValue: true, awaitPromise: true } })); });
    // wait for the audio to start (the app starts it without a gesture)
    let state = null;
    for (let i = 0; i < 40; i++) {
      state = await evaluate("({ cls: document.body.className, mode: window.__rqWebHost && window.__rqWebHost.audioMode, running: window.__rqWebHost && window.__rqWebHost.running, w: innerWidth, h: innerHeight, midi: typeof navigator.requestMIDIAccess, keys: document.querySelectorAll('.wkey').length, status: (document.getElementById('audio-info')||{}).textContent })");
      if (state && state.running) break;
      await sleep(250);
    }
    console.log('     ' + JSON.stringify(state));
    check(state && state.running && state.mode === 'AudioWorklet', `audio running in an AudioWorklet (${state && state.status})`);
    check(state && /\bweb\b/.test(state.cls), 'web host active');
    check(state && state.midi === 'function', 'Web MIDI shim present');
    const portrait = state && state.h > state.w;
    check(state && (!/compact/.test(state.cls) || (portrait ? state.keys === 15 : state.keys === 22)), `layout ${/compact/.test(state.cls) ? (portrait ? 'portrait' : 'landscape') : 'desktop'} with ${state && state.keys} white keys at ${state && state.w}x${state && state.h}`);
    // is the device actually rendering audio? (a headset that nobody wears, or a screen that is off, stops the clock)
    const t0 = await evaluate('window.__rqWebHost.time');
    await sleep(1000);
    const t1 = await evaluate('window.__rqWebHost.time');
    const clock = t1 > t0;
    console.log(`     audio clock ${t0.toFixed(2)} -> ${t1.toFixed(2)} s${clock ? '' : ' (not advancing: the device is not rendering audio right now; stats cannot be checked)'}`);
    // play a note through the bridge and read the stats
    await evaluate("window.__JUCE__.backend.emitEvent('noteOn', { note: 60, vel: 100 }); true");
    await sleep(700);
    const stats = await evaluate("new Promise(r => { window.__JUCE__.backend.addEventListener('stats', (s) => r({ voices: s.voices, peak: s.peak })); setTimeout(() => r({ voices: -1, peak: 0, timeout: true }), 4000); })");
    await evaluate("window.__JUCE__.backend.emitEvent('noteOff', { note: 60 }); true");
    console.log('     ' + JSON.stringify(stats));
    if (clock) check(stats && stats.voices > 0, stats && stats.timeout ? 'no stats message within 4 s although the audio clock runs' : 'a note sounds (voices > 0)');
    else console.log('skip a note sounds (voices > 0): the audio clock is stopped on this device');
    const midi = await evaluate("navigator.requestMIDIAccess().then(a => Array.from(a.inputs.values()).map(i => i.name)).catch(e => 'error: ' + e.message)");
    console.log('     MIDI inputs: ' + JSON.stringify(midi));
    ws.close();
  } finally {
    try { run('forward', '--remove', 'tcp:9229'); } catch (e) { /* ignore */ }
  }
  console.log(fails ? `${fails} FAILURE(S)` : 'DEVICE CHECK OK');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error('FAIL ' + (e.stack || e)); process.exit(1); });
