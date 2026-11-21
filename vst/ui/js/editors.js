// ============================================================================
//  Page-specific editors and screens:
//    PartialScreen  - the stiff-string partial series for the current settings
//    HammerScreen   - the felt force curve
//    VelocityEditor - the velocity -> hammer-speed curve (drag to bend)
//    MicDiagram     - a top-down piano with the virtual microphones
//    DetuneEditor   - per-note detune bars over an 88-key strip
// ============================================================================
'use strict';

const SVG_NS = 'http://www.w3.org/2000/svg';
function svgEl(tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  return e;
}

// ---- stiff-string partial series ------------------------------------------------------------
class PartialScreen {
  constructor(parent) {
    const s = document.createElement('div');
    s.className = 'screen tall';
    s.innerHTML = `<svg viewBox="0 0 240 100" preserveAspectRatio="none"><g class="bars"></g><path class="scr-line2" d=""/></svg><div class="scr-text"></div>`;
    parent.appendChild(s);
    this.bars = s.querySelector('.bars');
    this.line = s.querySelector('.scr-line2');
    this.text = s.querySelector('.scr-text');
  }
  // B: inharmonicity at C4 (scaled), decay: decay multiplier, stiffness: loss multiplier, position: strike ratio multiplier
  update(inharm, stiffness, decay, position, unison) {
    const B = 3.8e-4 * inharm, N = 32, f0 = 261.6;
    const strike = 0.12 * position;
    this.bars.innerHTML = '';
    for (let n = 1; n <= N; n++) {
      const f = n * f0 * Math.sqrt((1 + B * n * n) / (1 + B));
      const shape = Math.abs(Math.sin(n * Math.PI * strike));
      const loss = (0.2 + 0.75 * (f / 1000) * stiffness + 0.13 * (f / 1000) ** 2 * stiffness) / decay;
      const amp = shape / Math.sqrt(n) * Math.exp(-loss * 0.25);
      const x = 6 + (n - 1) * (228 / N);
      const h = clamp(amp, 0, 1) * 84;
      const harmonicX = 6 + (f / (f0 * N * 1.02)) * 228 * (N / (N));
      const b = svgEl('rect', { class: 'scr-bar' + (shape < 0.15 ? ' dim' : ''), x: (x - 1.5).toFixed(1), y: (92 - h).toFixed(1), width: 3, height: h.toFixed(1), rx: 1 });
      this.bars.appendChild(b);
      void harmonicX;
    }
    // the inharmonic stretch: where partial n sits relative to n*f0 (a rising curve)
    const pts = [];
    for (let n = 1; n <= N; n++) {
      const dev = 1200 * Math.log2(Math.sqrt((1 + B * n * n) / (1 + B)));
      pts.push(`${(6 + (n - 1) * (228 / N)).toFixed(1)},${(92 - clamp(dev / 120, 0, 1) * 84).toFixed(1)}`);
    }
    this.line.setAttribute('d', 'M' + pts.join(' L'));
    const dev32 = 1200 * Math.log2(Math.sqrt((1 + B * 1024) / (1 + B)));
    this.text.textContent = `C4  B=${(B * 1e4).toFixed(2)}e-4  partial 32 +${dev32.toFixed(0)} ct  unison ${unison.toFixed(1)} ct`;
  }
}

// ---- hammer felt force curve -----------------------------------------------------------------
class HammerScreen {
  constructor(parent) {
    const s = document.createElement('div');
    s.className = 'screen tall';
    s.innerHTML = `<svg viewBox="0 0 240 100" preserveAspectRatio="none"><path class="scr-grid" d="M0 92 H240 M6 0 V100"/><path class="scr-line2" d=""/><path class="scr-line" d=""/></svg><div class="scr-text"></div>`;
    parent.appendChild(s);
    this.soft = s.querySelector('.scr-line2');
    this.line = s.querySelector('.scr-line');
    this.text = s.querySelector('.scr-text');
  }
  update(hardness, felt, exponent, mass) {
    const K = felt * Math.pow(4, 2 * (hardness - 0.5));
    const p = 2.55 + exponent + 0.6 * (hardness - 0.5);
    const curve = (k, pp) => {
      const pts = [];
      for (let i = 0; i <= 60; i++) {
        const xi = i / 60;                              // compression 0..1 (relative)
        const F = k * Math.pow(xi, pp);
        pts.push(`${(6 + xi * 228).toFixed(1)},${(92 - clamp(F / 4, 0, 1) * 86).toFixed(1)}`);
      }
      return 'M' + pts.join(' L');
    };
    this.soft.setAttribute('d', curve(1, 2.55));
    this.line.setAttribute('d', curve(K, p));
    this.text.textContent = `F = K·ξ^p   K x${K.toFixed(2)}  p ${p.toFixed(2)}  mass x${mass.toFixed(2)}`;
  }
}

// ---- velocity curve editor ------------------------------------------------------------------
class VelocityEditor {
  // onChange({ dynamics, velCurve }) when dragged; the two knobs mirror it
  constructor(parent, onChange, onGesture) {
    this.onChange = onChange;
    this.onGesture = onGesture;
    const s = document.createElement('div');
    s.className = 'screen tall vel-editor';
    s.innerHTML = `<svg viewBox="0 0 240 100" preserveAspectRatio="none" tabindex="0">
      <path class="scr-grid" d="M6 92 H234 M6 8 V92 M66 8 V92 M126 8 V92 M186 8 V92 M6 50 H234"/>
      <path class="scr-line2" d=""/><path class="scr-line" d=""/><circle class="vel-dot" r="3"/></svg>
      <div class="scr-text left">pp</div><div class="scr-text">ff</div>`;
    parent.appendChild(s);
    this.svg = s.querySelector('svg');
    this.lin = s.querySelector('.scr-line2');
    this.line = s.querySelector('.scr-line');
    this.dot = s.querySelector('.vel-dot');
    this.dynamics = 0.5; this.curve = 0;
    this.drag = null;
    this.svg.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      this.svg.setPointerCapture(e.pointerId);
      this.drag = { x: e.clientX, y: e.clientY, c: this.curve, d: this.dynamics };
      if (this.onGesture) this.onGesture(true);
    });
    this.svg.addEventListener('pointermove', (e) => {
      if (!this.drag) return;
      const r = this.svg.getBoundingClientRect();
      const c = clamp(this.drag.c + (this.drag.y - e.clientY) / r.height * 2.2, -1, 1);
      const d = clamp(this.drag.d + (e.clientX - this.drag.x) / r.width * 1.2, 0, 1);
      this.curve = c; this.dynamics = d;
      this.render();
      this.onChange({ velCurve: c, dynamics: d });
    });
    const end = () => { if (!this.drag) return; this.drag = null; if (this.onGesture) this.onGesture(false); };
    this.svg.addEventListener('pointerup', end);
    this.svg.addEventListener('pointercancel', end);
    this.svg.addEventListener('dblclick', () => { this.curve = 0; this.dynamics = 0.5; this.render(); this.onChange({ velCurve: 0, dynamics: 0.5 }); });
    this.render();
  }
  set(dynamics, curve) {
    if (this.drag) return;
    this.dynamics = dynamics; this.curve = curve;
    this.render();
  }
  // mirrors Hammer::velocityToSpeed in HammerModel.h (relative to ff)
  speed(vel01) {
    const e = Math.pow(2, -this.curve * 1.2);
    const x = Math.pow(clamp(vel01, 1 / 127, 1), e);
    const ratio = Math.pow(6.0 / 0.25, 0.5 + this.dynamics);
    const top = 6.0 * Math.pow(0.5 + this.dynamics, 0.35);
    return top / Math.pow(ratio, 1 - x);
  }
  render() {
    const pts = [], lin = [];
    for (let i = 0; i <= 64; i++) {
      const v = i / 64;
      const db = 20 * Math.log10(this.speed(v) / 6.0);          // dB relative to ff
      pts.push(`${(6 + v * 228).toFixed(1)},${(92 - clamp((db + 40) / 44, 0, 1) * 84).toFixed(1)}`);
      lin.push(`${(6 + v * 228).toFixed(1)},${(92 - clamp((20 * Math.log10(Math.max(v, 1 / 127)) + 40) / 44, 0, 1) * 84).toFixed(1)}`);
    }
    this.line.setAttribute('d', 'M' + pts.join(' L'));
    this.lin.setAttribute('d', 'M' + lin.join(' L'));
    const mid = 20 * Math.log10(this.speed(0.5) / 6.0);
    this.dot.setAttribute('cx', (6 + 0.5 * 228).toFixed(1));
    this.dot.setAttribute('cy', (92 - clamp((mid + 40) / 44, 0, 1) * 84).toFixed(1));
  }
}

// ---- microphone diagram -----------------------------------------------------------------------
class MicDiagram {
  constructor(parent) {
    const s = document.createElement('div');
    s.className = 'screen tall';
    s.innerHTML = `<svg class="mic-svg" viewBox="0 0 240 110" preserveAspectRatio="xMidYMid meet">
      <path class="mic-piano" d="M30 20 H150 C190 20 200 60 170 90 L30 90 Z"/>
      <path class="mic-board" d="M40 28 H146 C176 28 186 58 160 82 L40 82 Z"/>
      <path class="mic-bridge" d="M60 36 C110 46 150 60 150 78"/>
      <rect x="30" y="90" width="120" height="6" fill="#0d0c0e" stroke="#34303a" stroke-width="0.8"/>
      <g class="mics"></g><text class="lbl" x="90" y="106">player</text></svg>`;
    parent.appendChild(s);
    this.g = s.querySelector('.mics');
    this.lbl = s.querySelector('.lbl');
  }
  update(perspective, distance, spread, position, width) {
    const presets = [ [0.8, 0.6, false], [0.4, 0.9, false], [4.0, 0.5, true], [1.5, 1.0, false], [1.2, 0.0, false], [distance, spread, false] ];
    const [d, sp, flip] = presets[clamp(Math.round(perspective), 0, 5)];
    this.g.innerHTML = '';
    const cx = 90 + position * 30;
    const y = flip ? 20 - clamp(d, 0.3, 8) * 1.6 : 96 + clamp(d, 0.3, 8) * 1.6;
    const sep = sp * 46 * (0.4 + 0.6 * width);
    const mics = perspective === 4 ? [cx] : [cx - sep, cx + sep];
    for (const x of mics) {
      this.g.appendChild(svgEl('circle', { class: 'mic-cap', cx: x.toFixed(1), cy: clamp(y, 4, 106).toFixed(1), r: 7 }));
      this.g.appendChild(svgEl('circle', { class: 'mic-dot', cx: x.toFixed(1), cy: clamp(y, 4, 106).toFixed(1), r: 3 }));
    }
    this.lbl.setAttribute('y', flip ? 106 : 106);
    this.lbl.textContent = flip ? `audience · ${d.toFixed(1)} m` : `${PERSPECTIVES[clamp(Math.round(perspective), 0, 5)].toLowerCase()} · ${d.toFixed(1)} m`;
  }
}

// ---- per-note detune editor ------------------------------------------------------------------
class DetuneEditor {
  // onChange(noteIndex, cents) while dragging (throttled by the caller if needed)
  constructor(parent, onChange) {
    this.onChange = onChange;
    this.values = new Array(88).fill(0);
    const s = document.createElement('div');
    s.className = 'detune-editor';
    s.innerHTML = `<svg viewBox="0 0 880 150" preserveAspectRatio="none"><g class="keys"></g><line class="dt-zero" x1="0" y1="75" x2="880" y2="75"/><g class="bars"></g><g class="labels"></g></svg><div class="dt-readout">drag a key up or down · double-click resets it · ±50 ct</div>`;
    parent.appendChild(s);
    this.svg = s.querySelector('svg');
    this.readout = s.querySelector('.dt-readout');
    const keys = s.querySelector('.keys'), labels = s.querySelector('.labels');
    this.bars = s.querySelector('.bars');
    this.barEls = [];
    for (let i = 0; i < 88; i++) {
      const note = 21 + i;
      keys.appendChild(svgEl('rect', { class: 'dt-key' + (isBlackKey(note) ? ' black' : ''), x: i * 10, y: 0, width: 10, height: 150 }));
      const b = svgEl('rect', { class: 'dt-bar', x: i * 10 + 2, y: 75, width: 6, height: 0 });
      this.bars.appendChild(b);
      this.barEls.push(b);
      if (note % 12 === 0) labels.appendChild(Object.assign(svgEl('text', { class: 'dt-label', x: i * 10 + 5, y: 146 }), { textContent: midiNoteName(note) }));
    }
    this.drag = null;
    this.svg.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      this.svg.setPointerCapture(e.pointerId);
      const i = this.keyAt(e.clientX);
      this.drag = { i, y: e.clientY, v: this.values[i] };
    });
    this.svg.addEventListener('pointermove', (e) => {
      if (!this.drag) return;
      const r = this.svg.getBoundingClientRect();
      const v = clamp(this.drag.v + (this.drag.y - e.clientY) / r.height * 100 * (e.shiftKey ? 0.2 : 1), -50, 50);
      this.setNote(this.drag.i, v);
      this.readout.textContent = `${midiNoteName(21 + this.drag.i)}  ${v >= 0 ? '+' : ''}${v.toFixed(1)} ct`;
      this.onChange(this.drag.i, v);
    });
    const end = () => { this.drag = null; };
    this.svg.addEventListener('pointerup', end);
    this.svg.addEventListener('pointercancel', end);
    this.svg.addEventListener('dblclick', (e) => { const i = this.keyAt(e.clientX); this.setNote(i, 0); this.onChange(i, 0); });
  }
  keyAt(clientX) {
    const r = this.svg.getBoundingClientRect();
    return clamp(Math.floor((clientX - r.left) / r.width * 88), 0, 87);
  }
  setNote(i, v) {
    this.values[i] = v;
    const h = Math.abs(v) / 50 * 70;
    const b = this.barEls[i];
    b.setAttribute('y', (v >= 0 ? 75 - h : 75).toFixed(1));
    b.setAttribute('height', h.toFixed(1));
  }
  setAll(arr) {
    if (this.drag) return;
    for (let i = 0; i < 88; i++) this.setNote(i, arr[i] || 0);
  }
}
