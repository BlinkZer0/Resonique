// ============================================================================
//  JUCE bridge: talks to the C++ processor through JUCE 8's native integration
//  (window.__JUCE__.backend), which JUCE injects into the page.
// ============================================================================
'use strict';

class JuceEngine {
  constructor() {
    this.backend = (window.__JUCE__ && window.__JUCE__.backend) || null;
    this.connected = !!this.backend;
    this.handlers = {};
    if (this.backend) {
      for (const ev of ['params', 'notes', 'stats', 'config', 'presetList', 'preset', 'saveResult', 'message', 'tuning', 'ir']) {
        this.backend.addEventListener(ev, (data) => { if (this.handlers[ev]) this.handlers[ev](data); });
      }
    }
  }

  on(event, fn) { this.handlers[event] = fn; }
  emit(id, payload) { if (this.backend) this.backend.emitEvent(id, payload || {}); }

  uiReady() { this.emit('uiReady'); }

  // parameters
  setParam(id, value) { this.emit('setParam', { id, value }); }
  setParams(values) { this.emit('setParams', { values }); }
  beginGesture(id) { this.emit('gesture', { id, begin: true }); }
  endGesture(id) { this.emit('gesture', { id, begin: false }); }
  paramMenu(id, x, y) { this.emit('paramMenu', { id, x: Math.round(x), y: Math.round(y) }); }

  // playing
  noteOn(note, vel) { this.emit('noteOn', { note, vel }); }
  noteOff(note) { this.emit('noteOff', { note }); }
  panic() { this.emit('panic'); }
  pedal(cc, value) { this.emit('pedal', { cc, value }); }

  // presets
  loadPreset(id) { this.emit('loadPreset', { id }); }
  savePreset(name, category, overwrite) { this.emit('savePreset', { name, category, overwrite: !!overwrite }); }
  deletePreset(id) { this.emit('deletePreset', { id }); }
  loadPresetFile() { this.emit('loadPresetFile'); }
  exportPreset() { this.emit('exportPreset'); }
  openPresetFolder() { this.emit('openPresetFolder'); }
  initPreset() { this.emit('initPreset'); }
  factoryReset() { this.emit('factoryReset'); }
  setFavourite(id, on) { this.emit('setFavourite', { id, on: !!on }); }

  // tuning
  setDetune(note, cents) { this.emit('setDetune', { note, cents }); }
  resetDetune() { this.emit('resetDetune'); }
  loadScala() { this.emit('loadScala'); }
  clearScala() { this.emit('clearScala'); }

  // impulse response
  loadIR() { this.emit('loadIR'); }
  clearIR() { this.emit('clearIR'); }
}
