// ============================================================================
//  Parameter controls besides the knob (family widgets). Same conventions:
//  setValue(v) updates the display, onChange(v) fires on user edits.
//    ToggleButton - bool param   (click; double-click = default)
//    Segmented    - choice param (click; wheel / arrow keys step; double-click = default)
//    SelectBox    - choice param (native dropdown; wheel steps; double-click = default)
//    PedalButton  - a latching pedal (not a parameter): click toggles, sends a CC
// ============================================================================
'use strict';

class ToggleButton {
  constructor(def, value, onChange, label) {
    this.def = def;
    this.onChange = onChange;
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'power';
    b.innerHTML = `<span class="led"></span><span>${label || 'ON'}</span>`;
    b.addEventListener('click', () => this.onChange(this.value >= 0.5 ? 0 : 1));
    this.el = b;
    this.setValue(value);
  }
  setValue(v) {
    this.value = v;
    this.el.classList.toggle('on', v >= 0.5);
    this.el.setAttribute('aria-pressed', v >= 0.5);
  }
}

class Segmented {
  constructor(def, value, onChange, labels) {
    this.def = def;
    this.onChange = onChange;
    const wrap = document.createElement('div');
    wrap.className = 'seg';
    wrap.setAttribute('role', 'radiogroup');
    wrap.setAttribute('aria-label', def.name);
    this.buttons = (labels || def.choices).map((label, i) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.textContent = label;
      b.setAttribute('role', 'radio');
      b.addEventListener('click', () => this.pick(i));
      wrap.appendChild(b);
      return b;
    });
    wrap.addEventListener('wheel', (e) => { e.preventDefault(); this.pick(this.value + (e.deltaY < 0 ? 1 : -1)); }, { passive: false });
    wrap.addEventListener('keydown', (e) => {
      const dir = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
      if (dir) { e.preventDefault(); e.stopPropagation(); this.pick(this.value + dir); this.buttons[this.value].focus(); }
    });
    wrap.addEventListener('dblclick', () => this.pick(this.def.def));
    this.el = wrap;
    this.setValue(value);
  }
  pick(i) {
    i = clamp(Math.round(i), 0, this.buttons.length - 1);
    if (i !== this.value) this.onChange(i);
  }
  setValue(v) {
    this.value = clamp(Math.round(v), 0, this.buttons.length - 1);
    this.buttons.forEach((b, i) => { b.classList.toggle('on', i === this.value); b.setAttribute('aria-checked', i === this.value); });
  }
}

class SelectBox {
  constructor(def, value, onChange, labels) {
    this.def = def;
    this.onChange = onChange;
    const s = document.createElement('select');
    s.className = 'param-select';
    s.setAttribute('aria-label', def.name);
    (labels || def.choices).forEach((label, i) => s.appendChild(new Option(label, String(i))));
    s.addEventListener('change', () => this.onChange(Number(s.value)));
    s.addEventListener('wheel', (e) => {
      e.preventDefault();
      const i = clamp(this.value + (e.deltaY < 0 ? -1 : 1), 0, s.options.length - 1);
      if (i !== this.value) this.onChange(i);
    }, { passive: false });
    s.addEventListener('dblclick', () => { if (this.value !== this.def.def) this.onChange(this.def.def); });
    s.addEventListener('keydown', (e) => e.stopPropagation()); // arrow keys belong to the dropdown, not the piano
    this.el = s;
    this.setValue(value);
  }
  setValue(v) {
    this.value = clamp(Math.round(v), 0, this.el.options.length - 1);
    this.el.value = String(this.value);
  }
}

class PedalButton {
  // cc: 64 sustain, 66 sostenuto, 67 soft. Click latches; the plugin reports the state back.
  constructor(label, cc, onChange) {
    this.cc = cc;
    this.onChange = onChange;
    this.value = 0;
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'pedal-btn';
    b.innerHTML = `<span class="pedal-shoe"></span><span class="pedal-label">${label}</span>`;
    b.addEventListener('click', () => this.onChange(this.cc, this.value >= 0.5 ? 0 : 127));
    this.el = b;
  }
  setValue(v) {
    this.value = v;
    this.el.classList.toggle('on', v >= 0.5);
    this.el.classList.toggle('half', v > 0.05 && v < 0.5);
  }
}
