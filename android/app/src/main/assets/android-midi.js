// Web MIDI for Android WebView (which has none). Implements the subset of
// navigator.requestMIDIAccess the web host uses, backed by MidiBridge.java
// (window.AndroidMidi). Loaded before the page's own scripts.
(function () {
  'use strict';
  const bridge = window.AndroidMidi;
  if (!bridge) return;

  const inputs = new Map();
  let access = null;

  function makeInput(d) {
    return {
      id: d.id, name: d.name, manufacturer: d.manufacturer || '', version: '',
      type: 'input', state: 'connected', connection: 'open',
      onmidimessage: null, onstatechange: null,
      addEventListener() {}, removeEventListener() {},
      open() { return Promise.resolve(this); }, close() { return Promise.resolve(this); },
    };
  }

  // Replace the input list; fire statechange for each port that came or went.
  function sync(list) {
    const seen = new Set();
    const changed = [];
    for (const d of list) {
      seen.add(d.id);
      if (!inputs.has(d.id)) { const p = makeInput(d); inputs.set(d.id, p); changed.push(p); }
    }
    for (const [id, p] of [...inputs]) {
      if (!seen.has(id)) { inputs.delete(id); p.state = 'disconnected'; p.connection = 'closed'; changed.push(p); }
    }
    return changed;
  }

  window.__androidMidi = {
    devices(list) {
      const changed = sync(list);
      if (!access) return;
      for (const port of changed) {
        const ev = { port, type: 'statechange' };
        if (typeof port.onstatechange === 'function') port.onstatechange(ev);
        if (typeof access.onstatechange === 'function') access.onstatechange(ev);
      }
    },
    receive(batch) {
      const t = performance.now();
      for (const m of batch) {
        const port = inputs.get(m[0]);
        if (!port || typeof port.onmidimessage !== 'function') continue;
        port.onmidimessage({ data: new Uint8Array(m.slice(1)), timeStamp: t, target: port, type: 'midimessage' });
      }
    },
  };

  navigator.requestMIDIAccess = function () {
    if (!bridge.isAvailable()) {
      return Promise.reject(new DOMException('this device has no MIDI support', 'NotSupportedError'));
    }
    if (!access) {
      access = {
        inputs, outputs: new Map(), sysexEnabled: false, onstatechange: null,
        addEventListener() {}, removeEventListener() {},
      };
      sync(JSON.parse(bridge.start()));
    }
    return Promise.resolve(access);
  };
})();
