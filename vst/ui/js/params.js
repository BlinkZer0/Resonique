// ============================================================================
//  Parameter definitions. Must match vst/Source/dsp/ParamTable.h exactly (IDs,
//  names, kinds, ranges, defaults, units, log skew, choice lists, order).
//  tests/check-params.ps1 compares the two. New parameters are only appended.
// ============================================================================
'use strict';

const INSTRUMENTS = ['Concert Grand', 'Studio Grand', 'Upright', 'Vintage Upright', 'Bright Pop Grand',
                     'Dark Jazz Grand', 'Felt Piano', 'Prepared Piano', 'Honky-Tonk', 'Experimental Grand'];
const REVERB_TYPES = ['Room', 'Chamber', 'Hall', 'Stage', 'Impulse Response'];
const PERSPECTIVES = ['Player', 'Close Stereo', 'Concert Audience', 'Wide Stereo', 'Mono', 'Custom'];
const PEDAL_MODES = ['Continuous', 'Switch'];
const TEMPERAMENTS = ['Equal', 'Pythagorean', 'Just Intonation', 'Meantone 1/4', 'Werckmeister III',
                      'Kirnberger III', 'Vallotti', 'Young', 'Custom / Scala'];
const ROOTS = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const NUM_VOICES = 128;

const choiceParam = (id, name, group, choices, def) =>
  ({ id, name, group, type: 'choice', min: 0, max: choices.length - 1, step: 1, def, choices });
const f = (id, name, group, min, max, def, unit, extra) => Object.assign({ id, name, group, type: 'float', min, max, def, unit }, extra || {});
const intParam = (id, name, group, min, max, def, unit) => ({ id, name, group, type: 'int', min, max, def, unit, step: 1 });

const PARAM_DEFS = [
  // ---- main page
  choiceParam('instrument', 'Instrument', 'main', INSTRUMENTS, 0),
  f('master', 'Master', 'output', 0, 1, 0.8, '%', { noPreset: true }),
  f('toneBass', 'Bass', 'output', -12, 12, 0, 'dB', { bipolar: true }),
  f('toneTreble', 'Treble', 'output', -12, 12, 0, 'dB', { bipolar: true }),
  f('hammerHardness', 'Hardness', 'main', 0, 1, 0.5, '%'),
  f('dynamics', 'Dynamics', 'perf', 0, 1, 0.5, '%'),
  f('velCurve', 'Curve', 'perf', -1, 1, 0, 'bip', { bipolar: true }),
  f('stringResonance', 'Str. Reso', 'main', 0, 1, 0.5, '%'),
  f('soundboardResonance', 'Board Reso', 'main', 0, 1, 0.5, '%'),
  f('stereoWidth', 'Width', 'output', 0, 1, 0.7, '%'),
  f('reverbMix', 'Reverb', 'room', 0, 1, 0.18, '%'),
  f('reverbSize', 'Size', 'room', 0, 1, 0.5, '%'),
  f('reverbDamp', 'Damping', 'room', 0, 1, 0.5, '%'),
  choiceParam('reverbType', 'Type', 'room', REVERB_TYPES, 2),
  f('reverbPredelay', 'Predelay', 'room', 0, 100, 12, 'ms'),
  // ---- physical model page
  f('inharmonicity', 'Inharmonic.', 'strings', 0.25, 4, 1, 'x', { skew: 'log' }),
  f('stringStiffness', 'Stiffness', 'strings', 0.25, 4, 1, 'x', { skew: 'log' }),
  f('stringDecay', 'Decay', 'strings', 0.25, 4, 1, 'x', { skew: 'log' }),
  f('hammerPosition', 'Position', 'hammer', 0.5, 2, 1, 'x', { skew: 'log' }),
  f('hammerMass', 'Mass', 'hammer', 0.5, 2, 1, 'x', { skew: 'log' }),
  f('feltStiffness', 'Felt K', 'hammer', 0.25, 4, 1, 'x', { skew: 'log' }),
  f('feltExponent', 'Felt Exp.', 'hammer', -1, 1, 0, 'exp', { bipolar: true }),
  f('unisonDetune', 'Unison', 'strings', 0, 6, 1.0, 'ct'),
  f('damperEfficiency', 'Dampers', 'board', 0.25, 4, 1, 'x', { skew: 'log' }),
  f('bridgeCoupling', 'Bridge', 'strings', 0, 1, 0.5, '%'),
  f('soundboardSize', 'Board Size', 'board', 0.7, 1.4, 1, 'x', { skew: 'log' }),
  f('soundboardDamping', 'Board Damp', 'board', 0.3, 3, 1, 'x', { skew: 'log' }),
  f('soundboardBrightness', 'Brightness', 'board', -1, 1, 0, 'bip', { bipolar: true }),
  // ---- microphones
  choiceParam('micPerspective', 'Perspective', 'mics', PERSPECTIVES, 0),
  f('micDistance', 'Distance', 'mics', 0.3, 8, 1.2, 'm', { skew: 'log' }),
  f('micSpread', 'Spread', 'mics', 0, 1, 0.6, '%'),
  f('micPosition', 'Position', 'mics', -1, 1, 0, 'bip', { bipolar: true }),
  f('earlyReflections', 'Reflections', 'mics', 0, 1, 0.3, '%'),
  // ---- performance
  intParam('polyphony', 'Polyphony', 'perf', 8, 128, 64, ''),
  choiceParam('pedalMode', 'Pedal Mode', 'perf', PEDAL_MODES, 0),
  f('halfPedalPoint', 'Half Point', 'perf', 0.1, 0.9, 0.5, '%'),
  f('softPedalDepth', 'Soft Depth', 'perf', 0, 1, 0.6, '%'),
  intParam('pitchBendRange', 'Bend Range', 'perf', 0, 12, 2, 'st'),
  // ---- mechanical noises
  f('noiseHammer', 'Hammer', 'noise', 0, 1, 0.5, '%'),
  f('noiseKeyRelease', 'Release', 'noise', 0, 1, 0.4, '%'),
  f('noiseDamper', 'Damper', 'noise', 0, 1, 0.4, '%'),
  f('noisePedal', 'Pedal', 'noise', 0, 1, 0.4, '%'),
  f('noiseMaster', 'Amount', 'noise', 0, 1, 0.5, '%'),
  // ---- tuning
  choiceParam('temperament', 'Temperament', 'tuning', TEMPERAMENTS, 0),
  choiceParam('temperamentRoot', 'Root', 'tuning', ROOTS, 0),
  f('concertPitch', 'A4', 'tuning', 415, 466, 440, 'Hz'),
  f('stretch', 'Stretch', 'tuning', 0, 1, 0.6, '%'),
  // ---- level matching
  f('presetGain', 'Preset Gain', 'output', -24, 12, 0, 'dB', { bipolar: true }),
];

const PARAM_BY_ID = Object.fromEntries(PARAM_DEFS.map(d => [d.id, d]));

function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

function paramToNorm(def, v) {
  v = clamp(v, def.min, def.max);
  if (def.skew === 'log') return Math.log(v / def.min) / Math.log(def.max / def.min);
  return (v - def.min) / (def.max - def.min);
}
function paramFromNorm(def, n) {
  n = clamp(n, 0, 1);
  let v = def.skew === 'log' ? def.min * Math.pow(def.max / def.min, n) : def.min + (def.max - def.min) * n;
  if (def.step) v = Math.round(v / def.step) * def.step;
  return clamp(v, def.min, def.max);
}

function formatParam(def, v) {
  if (def.type === 'choice') return def.choices[clamp(Math.round(v), 0, def.choices.length - 1)];
  switch (def.unit) {
    case '%': return `${Math.round(v * 100)}%`;
    case 'bip': return `${v > 0.004 ? '+' : ''}${Math.round(v * 100)}%`;
    case 'dB': return `${v > 0.05 ? '+' : ''}${v.toFixed(1)} dB`;
    case 'Hz': return `${v.toFixed(1)} Hz`;
    case 'ct': return `${v.toFixed(2)} ct`;
    case 'st': return `${Math.round(v)} st`;
    case 'm': return `${v.toFixed(2)} m`;
    case 'ms': return `${Math.round(v)} ms`;
    case 'x': return `x${v.toFixed(2)}`;
    case 'exp': return `${v > 0.004 ? '+' : ''}${v.toFixed(2)}`;
    default: return def.type === 'int' ? String(Math.round(v)) : v.toFixed(2);
  }
}

function defaultParamValues() {
  const v = {};
  for (const d of PARAM_DEFS) v[d.id] = d.def;
  return v;
}
