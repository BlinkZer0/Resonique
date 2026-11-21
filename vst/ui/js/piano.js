// ============================================================================
//  SVG piano (family widget). Inverted colours: naturals are black, sharps
//  are ivory. Click/touch to play, drag for glissando. Velocity = how far down
//  the key you click (lower = harder). Default: the full 88 keys (A0..C8).
// ============================================================================
'use strict';

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const BLACK_PCS = new Set([1, 3, 6, 8, 10]);

function midiNoteName(n) { return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1); }
function isBlackKey(n) { return BLACK_PCS.has(n % 12); }

class Piano {
  constructor(container, { numKeys = 88, baseNote = 21, onNoteOn, onNoteOff, whiteWidth = 22, whiteHeight = 118, labels = true }) {
    this.numKeys = numKeys;
    this.base = baseNote;
    this.onNoteOn = onNoteOn;
    this.onNoteOff = onNoteOff;
    this.active = new Set();     // sounding MIDI notes (from any source)
    this.pointers = new Map();   // pointerId -> note
    this.keys = [];
    this.WW = whiteWidth; this.WH = whiteHeight; this.showLabels = labels;
    this.build(container);
  }

  build(container) {
    const WW = this.WW, WH = this.WH, BW = Math.round(WW * 0.6), BH = Math.round(WH * 0.62), TOP = 8;
    const whites = [], blacks = [];
    let wi = 0;
    for (let i = 0; i < this.numKeys; i++) {
      const note = this.base + i;
      if (isBlackKey(note)) {
        blacks.push(`<rect class="bkey" data-idx="${i}" x="${wi * WW - BW / 2}" y="${TOP}" width="${BW}" height="${BH}" rx="2"/>`);
      } else {
        whites.push(`<rect class="wkey" data-idx="${i}" x="${wi * WW + 0.75}" y="${TOP}" width="${WW - 1.5}" height="${WH}" rx="2.5"/>`);
        if (this.showLabels && note % 12 === 0) whites.push(`<text class="klabel" data-label="${i}" x="${wi * WW + WW / 2}" y="${TOP + WH - 8}"></text>`);
        wi++;
      }
    }
    const W = wi * WW;
    container.innerHTML = `
      <svg class="piano-svg" viewBox="0 0 ${W} ${TOP + WH + 2}" preserveAspectRatio="xMidYMid meet">
        ${whites.join('')}
        ${blacks.join('')}
        <rect class="felt" x="0" y="0" width="${W}" height="${TOP + 1}"/>
      </svg>`;
    this.svg = container.querySelector('svg');
    this.keys = Array.from({ length: this.numKeys }, (_, i) => this.svg.querySelector(`[data-idx="${i}"]`));
    this.labels = Array.from(this.svg.querySelectorAll('[data-label]'));
    this.bindPointer();
    this.render();
  }

  setNote(note, on) {
    if (on) this.active.add(note); else this.active.delete(note);
    const i = note - this.base;
    if (i >= 0 && i < this.numKeys) this.keys[i].classList.toggle('down', on);
  }

  clear() {
    this.active.clear();
    this.render();
  }

  render() {
    this.keys.forEach((k, i) => k.classList.toggle('down', this.active.has(this.base + i)));
    this.labels.forEach(t => { t.textContent = midiNoteName(this.base + Number(t.dataset.label)); });
  }

  hit(x, y) {
    const t = document.elementFromPoint(x, y);
    if (!t || !this.svg.contains(t) || !t.dataset.idx) return null;
    const r = t.getBoundingClientRect();
    const depth = clamp((y - r.top) / r.height, 0, 1);
    return { note: this.base + Number(t.dataset.idx), vel: Math.round(24 + depth * 103) };
  }

  bindPointer() {
    const svg = this.svg;
    const press = (id, x, y) => {
      const h = this.hit(x, y);
      if (!h) return;
      this.pointers.set(id, h.note);
      this.onNoteOn(h.note, h.vel);
    };
    const lift = (id) => {
      const n = this.pointers.get(id);
      if (n !== undefined && n !== null) this.onNoteOff(n);
      this.pointers.delete(id);
    };
    svg.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      svg.setPointerCapture(e.pointerId);
      press(e.pointerId, e.clientX, e.clientY);
    });
    svg.addEventListener('pointermove', (e) => {
      if (!this.pointers.has(e.pointerId)) return;
      const h = this.hit(e.clientX, e.clientY);
      const cur = this.pointers.get(e.pointerId);
      const next = h ? h.note : null;
      if (next === cur) return;
      if (cur !== null) this.onNoteOff(cur);
      this.pointers.set(e.pointerId, next);
      if (h) this.onNoteOn(h.note, h.vel);
    });
    svg.addEventListener('pointerup', (e) => lift(e.pointerId));
    svg.addEventListener('pointercancel', (e) => lift(e.pointerId));
  }
}
