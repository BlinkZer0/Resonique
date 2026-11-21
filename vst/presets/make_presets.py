#!/usr/bin/env python3
"""Generates vst/presets/factory_presets.json from recipes.

Every preset is a dictionary of *physical* parameters (instrument, hammers, strings,
dampers, soundboard, tuning, noises) plus the room/mic settings that suit it. The
recipes are written against a small builder so each one states what makes it
different; the script refuses presets whose synthesis parameters are near-identical
to another preset's (the same check ResoniqueTests --presets performs).

Usage:
    python make_presets.py                 writes factory_presets.json
    python make_presets.py --match in.csv  applies level trims from `ResoniqueTests --loudness`
"""
import json, math, os, sys, csv

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'factory_presets.json')
TRIMS = os.path.join(HERE, 'trims.json')
TARGET_DBFS = -16.0

INSTRUMENTS = ['Concert Grand', 'Studio Grand', 'Upright', 'Vintage Upright', 'Bright Pop Grand',
               'Dark Jazz Grand', 'Felt Piano', 'Prepared Piano', 'Honky-Tonk', 'Experimental Grand']
PERSPECTIVES = ['Player', 'Close Stereo', 'Concert Audience', 'Wide Stereo', 'Mono', 'Custom']
REVERBS = ['Room', 'Chamber', 'Hall', 'Stage', 'Impulse Response']
TEMPERAMENTS = ['Equal', 'Pythagorean', 'Just Intonation', 'Meantone 1/4', 'Werckmeister III',
                'Kirnberger III', 'Vallotti', 'Young', 'Custom / Scala']

# parameter ranges (mirrors ParamTable.h) for clamping and the distinctness check
RANGES = {
    'instrument': (0, 9), 'toneBass': (-12, 12), 'toneTreble': (-12, 12), 'hammerHardness': (0, 1), 'dynamics': (0, 1),
    'velCurve': (-1, 1), 'stringResonance': (0, 1), 'soundboardResonance': (0, 1), 'stereoWidth': (0, 1),
    'reverbMix': (0, 1), 'reverbSize': (0, 1), 'reverbDamp': (0, 1), 'reverbType': (0, 4), 'reverbPredelay': (0, 100),
    'inharmonicity': (0.25, 4), 'stringStiffness': (0.25, 4), 'stringDecay': (0.25, 4), 'hammerPosition': (0.5, 2),
    'hammerMass': (0.5, 2), 'feltStiffness': (0.25, 4), 'feltExponent': (-1, 1), 'unisonDetune': (0, 6),
    'damperEfficiency': (0.25, 4), 'bridgeCoupling': (0, 1), 'soundboardSize': (0.7, 1.4), 'soundboardDamping': (0.3, 3),
    'soundboardBrightness': (-1, 1), 'micPerspective': (0, 5), 'micDistance': (0.3, 8), 'micSpread': (0, 1),
    'micPosition': (-1, 1), 'earlyReflections': (0, 1), 'polyphony': (8, 128), 'pedalMode': (0, 1),
    'halfPedalPoint': (0.1, 0.9), 'softPedalDepth': (0, 1), 'pitchBendRange': (0, 12), 'noiseHammer': (0, 1),
    'noiseKeyRelease': (0, 1), 'noiseDamper': (0, 1), 'noisePedal': (0, 1), 'noiseMaster': (0, 1),
    'temperament': (0, 8), 'temperamentRoot': (0, 11), 'concertPitch': (415, 466), 'stretch': (0, 1), 'presetGain': (-24, 12),
}
LOG = {'inharmonicity', 'stringStiffness', 'stringDecay', 'hammerPosition', 'hammerMass', 'feltStiffness',
       'damperEfficiency', 'soundboardSize', 'soundboardDamping', 'micDistance'}
SYNTH = ['instrument', 'hammerHardness', 'dynamics', 'velCurve', 'stringResonance', 'soundboardResonance',
         'inharmonicity', 'stringStiffness', 'stringDecay', 'hammerPosition', 'hammerMass', 'feltStiffness', 'feltExponent',
         'unisonDetune', 'damperEfficiency', 'bridgeCoupling', 'soundboardSize', 'soundboardDamping', 'soundboardBrightness',
         'noiseMaster', 'temperament', 'stretch']
DEFAULTS = {
    'instrument': 0, 'toneBass': 0, 'toneTreble': 0, 'hammerHardness': 0.5, 'dynamics': 0.5, 'velCurve': 0,
    'stringResonance': 0.5, 'soundboardResonance': 0.5, 'stereoWidth': 0.7, 'reverbMix': 0.18, 'reverbSize': 0.5,
    'reverbDamp': 0.5, 'reverbType': 2, 'reverbPredelay': 12, 'inharmonicity': 1, 'stringStiffness': 1, 'stringDecay': 1,
    'hammerPosition': 1, 'hammerMass': 1, 'feltStiffness': 1, 'feltExponent': 0, 'unisonDetune': 1.0,
    'damperEfficiency': 1, 'bridgeCoupling': 0.5, 'soundboardSize': 1, 'soundboardDamping': 1, 'soundboardBrightness': 0,
    'micPerspective': 0, 'micDistance': 1.2, 'micSpread': 0.6, 'micPosition': 0, 'earlyReflections': 0.3,
    'polyphony': 64, 'pedalMode': 0, 'halfPedalPoint': 0.5, 'softPedalDepth': 0.6, 'pitchBendRange': 2,
    'noiseHammer': 0.5, 'noiseKeyRelease': 0.4, 'noiseDamper': 0.4, 'noisePedal': 0.4, 'noiseMaster': 0.5,
    'temperament': 0, 'temperamentRoot': 0, 'concertPitch': 440, 'stretch': 0.6, 'presetGain': 0,
}


def norm(pid, v):
    lo, hi = RANGES[pid]
    if pid in LOG:
        return math.log(v / lo) / math.log(hi / lo)
    return (v - lo) / (hi - lo)


class P:
    """One preset. Chainable setters; the builder names things in physical terms."""
    def __init__(self, name, instrument, desc=''):
        self.name, self.desc = name, desc
        self.p = {'instrument': INSTRUMENTS.index(instrument)}

    def set(self, **kw):
        for k, v in kw.items():
            if k not in RANGES:
                raise KeyError(k)
            lo, hi = RANGES[k]
            self.p[k] = min(hi, max(lo, v))
        return self

    # physical shorthands -------------------------------------------------------------------
    def hammer(self, hardness=None, felt=None, exponent=None, mass=None, position=None):
        d = {}
        if hardness is not None: d['hammerHardness'] = hardness
        if felt is not None: d['feltStiffness'] = felt
        if exponent is not None: d['feltExponent'] = exponent
        if mass is not None: d['hammerMass'] = mass
        if position is not None: d['hammerPosition'] = position
        return self.set(**d)

    def strings(self, inharm=None, stiffness=None, decay=None, unison=None, coupling=None):
        d = {}
        if inharm is not None: d['inharmonicity'] = inharm
        if stiffness is not None: d['stringStiffness'] = stiffness
        if decay is not None: d['stringDecay'] = decay
        if unison is not None: d['unisonDetune'] = unison
        if coupling is not None: d['bridgeCoupling'] = coupling
        return self.set(**d)

    def board(self, resonance=None, size=None, damping=None, brightness=None, sympathetic=None):
        d = {}
        if resonance is not None: d['soundboardResonance'] = resonance
        if size is not None: d['soundboardSize'] = size
        if damping is not None: d['soundboardDamping'] = damping
        if brightness is not None: d['soundboardBrightness'] = brightness
        if sympathetic is not None: d['stringResonance'] = sympathetic
        return self.set(**d)

    def dampers(self, efficiency=None, half=None):
        d = {}
        if efficiency is not None: d['damperEfficiency'] = efficiency
        if half is not None: d['halfPedalPoint'] = half
        return self.set(**d)

    def touch(self, dynamics=None, curve=None):
        d = {}
        if dynamics is not None: d['dynamics'] = dynamics
        if curve is not None: d['velCurve'] = curve
        return self.set(**d)

    def noises(self, master=None, hammer=None, release=None, damper=None, pedal=None):
        d = {}
        if master is not None: d['noiseMaster'] = master
        if hammer is not None: d['noiseHammer'] = hammer
        if release is not None: d['noiseKeyRelease'] = release
        if damper is not None: d['noiseDamper'] = damper
        if pedal is not None: d['noisePedal'] = pedal
        return self.set(**d)

    def mics(self, perspective, distance=None, spread=None, position=None, width=None, er=None):
        d = {'micPerspective': PERSPECTIVES.index(perspective)}
        if distance is not None: d['micDistance'] = distance
        if spread is not None: d['micSpread'] = spread
        if position is not None: d['micPosition'] = position
        if width is not None: d['stereoWidth'] = width
        if er is not None: d['earlyReflections'] = er
        return self.set(**d)

    def room(self, kind, mix, size=None, damp=None, predelay=None):
        d = {'reverbType': REVERBS.index(kind), 'reverbMix': mix}
        if size is not None: d['reverbSize'] = size
        if damp is not None: d['reverbDamp'] = damp
        if predelay is not None: d['reverbPredelay'] = predelay
        return self.set(**d)

    def tone(self, bass=None, treble=None):
        d = {}
        if bass is not None: d['toneBass'] = bass
        if treble is not None: d['toneTreble'] = treble
        return self.set(**d)

    def tuning(self, temperament=None, root=None, pitch=None, stretch=None):
        d = {}
        if temperament is not None: d['temperament'] = TEMPERAMENTS.index(temperament)
        if root is not None: d['temperamentRoot'] = root
        if pitch is not None: d['concertPitch'] = pitch
        if stretch is not None: d['stretch'] = stretch
        return self.set(**d)

    def copy(self, name, desc=''):
        q = P.__new__(P)
        q.name, q.desc, q.p = name, desc or self.desc, dict(self.p)
        return q


def synth_distance(a, b):
    d = 0.0
    for k in SYNTH:
        va, vb = a.get(k, DEFAULTS[k]), b.get(k, DEFAULTS[k])
        if k == 'instrument':
            d += 0 if va == vb else 1
        else:
            d += abs(norm(k, va) - norm(k, vb))
    return d


# ============================================================================================
#  The library
# ============================================================================================
CATEGORIES = []


def category(name, presets):
    CATEGORIES.append((name, presets))


# ---- Concert Grands --------------------------------------------------------------------------
cg = P('Concert Grand', 'Concert Grand', 'The reference instrument: a 2.7 m grand from the player\'s seat').mics('Player').room('Hall', 0.16, 0.55)
category('Concert Grands', [
    cg,
    cg.copy('Concert Grand Stage', 'The same grand from the first rows of a hall, lid fully up').mics('Concert Audience').room('Hall', 0.3, 0.7, 0.45, 20).board(resonance=0.55).strings(coupling=0.6, decay=1.1),
    cg.copy('Concert Grand Close', 'Spot mics under the lid: more hammer, less hall').mics('Close Stereo').room('Room', 0.1, 0.4).noises(master=0.6, hammer=0.7).board(brightness=0.2),
    cg.copy('Concert Grand Wide', 'A wide pair across the full keyboard, a little more unison shimmer').mics('Wide Stereo', width=0.95).room('Hall', 0.2, 0.6).board(sympathetic=0.65, resonance=0.55).strings(unison=1.3),
    cg.copy('Concert Grand Dark', 'Needled hammers, the lid on the short stick').hammer(hardness=0.32, felt=0.7).board(brightness=-0.35).tone(treble=-2).room('Hall', 0.18, 0.5),
    cg.copy('Concert Grand Brilliant', 'Voiced bright for a big hall').hammer(hardness=0.7, felt=1.5, exponent=0.2).strings(stiffness=0.8).board(brightness=0.3, resonance=0.55).room('Hall', 0.22, 0.65),
    cg.copy('Concert Grand Warm Hall', 'Softer felt and a large, damped hall').hammer(hardness=0.42).board(resonance=0.6, damping=0.85).room('Hall', 0.32, 0.8, 0.6, 24).mics('Concert Audience', width=0.8),
    cg.copy('Concert Grand Pure', 'Equal temperament, no stretch, dry: for sampling and tuning work').tuning(stretch=0.0).room('Room', 0.04, 0.3).noises(master=0.3).mics('Mono'),
    cg.copy('Concert Grand Recital', 'A recital hall with a close, present image; slightly heavier hammers').mics('Custom', distance=1.6, spread=0.75, er=0.4).room('Chamber', 0.24, 0.6, 0.45, 15).board(resonance=0.58).hammer(hardness=0.47, mass=1.1).strings(decay=0.95),
    cg.copy('Concert Grand Stretch 442', 'Orchestral pitch and a strong stretch, as a concert tuner leaves it').tuning(pitch=442, stretch=0.85).strings(unison=0.7).room('Hall', 0.18, 0.6),
    cg.copy('Concert Grand Open Lid', 'Full lid, lively board, lots of sympathetic bloom').board(resonance=0.72, sympathetic=0.8).strings(coupling=0.65).room('Hall', 0.2, 0.6),
    cg.copy('Concert Grand Soft Felt', 'A pianissimo-friendly voicing with a wide dynamic range').hammer(hardness=0.38, felt=0.6, exponent=-0.2).touch(dynamics=0.7).room('Hall', 0.2, 0.6),
    cg.copy('Concert Grand Heavy Hammers', 'Heavier hammers: slower contact, fatter attack').hammer(mass=1.5, hardness=0.5).strings(decay=1.15).room('Hall', 0.18, 0.55),
    cg.copy('Concert Grand Bass Bloom', 'More bridge coupling and a bigger board for the low end').board(size=1.2, resonance=0.6).strings(coupling=0.75, decay=1.2).tone(bass=1.5).room('Hall', 0.2, 0.65),
    cg.copy('Concert Grand Crisp', 'Strike point nearer the end of the string, cleaner partials').hammer(position=0.85, hardness=0.58).strings(inharm=0.9).room('Hall', 0.16, 0.5),
    cg.copy('Concert Grand Mono Broadcast', 'A single distant mono microphone, as on an old broadcast').mics('Mono').room('Chamber', 0.2, 0.5, 0.6).tone(bass=-1, treble=-1.5).noises(master=0.4),
])

# ---- Classical ----------------------------------------------------------------------------
cl = P('Classical Hall', 'Concert Grand', 'Audience perspective, long hall, moderate stretch').mics('Concert Audience').room('Hall', 0.34, 0.85, 0.5, 26).tuning(stretch=0.7)
category('Classical', [
    cl,
    cl.copy('Chamber Music', 'A small hall, close enough to hear the dampers').mics('Player').room('Chamber', 0.24, 0.55, 0.45, 14).noises(master=0.6, damper=0.6).board(resonance=0.55),
    cl.copy('Lieder Accompanist', 'Lid on the short stick, singer in front').hammer(hardness=0.4).board(brightness=-0.25).mics('Custom', distance=2.2, spread=0.5, er=0.35).room('Chamber', 0.26, 0.6),
    P('Baroque Werckmeister', 'Studio Grand', 'Werckmeister III at 415 Hz on a lighter, clearer instrument').tuning('Werckmeister III', 0, 415, 0.3).hammer(hardness=0.55, mass=0.8).strings(decay=0.8, unison=0.5).room('Chamber', 0.22, 0.5, 0.5),
    P('Meantone Fortepiano', 'Studio Grand', 'Quarter-comma meantone, light hammers, fast decay: a fortepiano flavour').tuning('Meantone 1/4', 0, 430, 0.2).hammer(hardness=0.6, mass=0.6, felt=1.6, exponent=0.3).strings(decay=0.55, inharm=1.4, unison=0.6).board(size=0.85, damping=1.5, resonance=0.4).dampers(efficiency=1.6).room('Room', 0.16, 0.4),
    P('Kirnberger Chapel', 'Concert Grand', 'Kirnberger III in a stone chapel').tuning('Kirnberger III', 0, 440, 0.5).mics('Concert Audience').room('Hall', 0.42, 0.95, 0.35, 35).board(resonance=0.6),
    P('Vallotti Salon', 'Dark Jazz Grand', 'Vallotti well temperament, warm salon piano').tuning('Vallotti', 0, 440, 0.5).hammer(hardness=0.45).room('Chamber', 0.26, 0.55, 0.5).mics('Player', width=0.6),
    P('Young Temperament Study', 'Studio Grand', 'Young\'s temperament, dry practice room').tuning('Young', 0, 440, 0.6).room('Room', 0.1, 0.35).noises(master=0.6),
    P('Pythagorean Early', 'Studio Grand', 'Pythagorean tuning for early music lines, light touch').tuning('Pythagorean', 0, 440, 0.2).hammer(hardness=0.52, mass=0.75).strings(decay=0.75).room('Chamber', 0.3, 0.6, 0.5),
    P('Just Intonation Drone', 'Experimental Grand', 'Just intonation on the long-string grand: pure fifths that bloom').tuning('Just Intonation', 0, 440, 0.0).board(sympathetic=0.9, resonance=0.7).strings(decay=1.6, coupling=0.7).room('Hall', 0.35, 0.9, 0.4, 30),
    P('Romantic Concert', 'Concert Grand', 'Big, singing tone for the late-Romantic repertoire').hammer(hardness=0.5, mass=1.2).strings(decay=1.3, coupling=0.6).board(resonance=0.65, size=1.1).mics('Concert Audience').room('Hall', 0.36, 0.9, 0.5, 30),
    P('Impressionist Haze', 'Concert Grand', 'Soft hammers, lots of pedal resonance and a wash of hall').hammer(hardness=0.33, felt=0.6).board(sympathetic=0.9, resonance=0.7, brightness=-0.2).dampers(efficiency=0.7, half=0.6).room('Hall', 0.45, 1.0, 0.55, 40).mics('Wide Stereo'),
    P('Études Dry', 'Studio Grand', 'A practice room: dry, clear, honest').room('Room', 0.07, 0.3).mics('Player').noises(master=0.55).hammer(hardness=0.55),
    P('Period Studio Grand', 'Studio Grand', 'A 19th-century parlour grand: thinner strings, quicker decay').strings(inharm=1.3, decay=0.7, unison=1.4).hammer(hardness=0.45, mass=0.8).board(size=0.9, damping=1.3).tuning(stretch=0.4).room('Chamber', 0.24, 0.5),
    P('Cathedral Organ-Piano', 'Experimental Grand', 'A huge reverberant space and a board that will not stop').board(resonance=0.8, size=1.35, sympathetic=0.9).strings(decay=1.8).room('Hall', 0.55, 1.0, 0.3, 60).mics('Concert Audience', width=0.9),
    P('Competition Hall', 'Concert Grand', 'Bright, projecting, from the jury\'s seats').hammer(hardness=0.62, felt=1.3).board(resonance=0.55, brightness=0.2).mics('Concert Audience').room('Hall', 0.3, 0.75, 0.5, 22),
    P('Soft Pedal Nocturne', 'Dark Jazz Grand', 'Una corda flavour baked in: a brushed, intimate tone').hammer(hardness=0.3, felt=0.5, exponent=-0.3).strings(unison=1.6, coupling=0.6).board(brightness=-0.3).set(softPedalDepth=0.9).room('Chamber', 0.3, 0.6, 0.6).mics('Player'),
])

# ---- Jazz -----------------------------------------------------------------------------------
jz = P('Jazz Club Grand', 'Dark Jazz Grand', 'A warm grand on a small stage').mics('Player').room('Room', 0.14, 0.45, 0.5).noises(master=0.6)
category('Jazz', [
    jz,
    jz.copy('Jazz Trio Studio', 'Close mics, dry, present, tight unisons').mics('Close Stereo', width=0.75).room('Room', 0.08, 0.35).hammer(hardness=0.5).board(brightness=0.1).strings(decay=0.85, unison=0.7),
    jz.copy('Ballad Grand', 'Softer felt, longer decay, a little hall').hammer(hardness=0.36, felt=0.7).strings(decay=1.3).board(sympathetic=0.7).room('Hall', 0.26, 0.6, 0.5, 18),
    jz.copy('Bebop Bright', 'Harder hammers for fast lines').hammer(hardness=0.66, felt=1.4, exponent=0.2).strings(decay=0.9).board(brightness=0.25).room('Room', 0.1, 0.4),
    jz.copy('Late Night Felt Jazz', 'Felted hammers in a smoky room').set(instrument=INSTRUMENTS.index('Felt Piano')).hammer(hardness=0.45).noises(master=0.7, hammer=0.8).room('Room', 0.2, 0.5, 0.6).mics('Close Stereo'),
    jz.copy('Stride Upright', 'A bar upright with a bit of swing in the unisons').set(instrument=INSTRUMENTS.index('Upright')).strings(unison=2.2).hammer(hardness=0.6).room('Room', 0.12, 0.4).noises(master=0.7),
    jz.copy('Modal Dark', 'Very dark, lid closed, heavy sustain').hammer(hardness=0.25, felt=0.45, mass=1.3).board(brightness=-0.5, resonance=0.6).tone(treble=-3).strings(decay=1.25).room('Chamber', 0.22, 0.6, 0.65),
    jz.copy('Cocktail Piano', 'A bright, slightly detuned lounge grand').strings(unison=1.8, decay=0.85).hammer(hardness=0.6).board(size=0.9).room('Chamber', 0.2, 0.5).tone(treble=1.5),
    jz.copy('Rhythm Section Grand', 'Punchy and controlled, sits in a band').hammer(hardness=0.58, mass=1.1).strings(decay=0.8).dampers(efficiency=1.4).board(resonance=0.4).room('Room', 0.08, 0.3).mics('Close Stereo'),
    jz.copy('Vinyl Jazz', 'Rolled-off, older recording feel').set(instrument=INSTRUMENTS.index('Vintage Upright')).tone(bass=1, treble=-4).hammer(hardness=0.4).room('Room', 0.15, 0.45, 0.7).mics('Mono').noises(master=0.8),
    jz.copy('Solo Piano Hall', 'A solo recital in a mid-size hall').set(instrument=INSTRUMENTS.index('Concert Grand')).hammer(hardness=0.45).mics('Concert Audience').room('Hall', 0.3, 0.7, 0.5, 20).board(resonance=0.6),
    jz.copy('Brushed Ballad', 'A tiny unison shimmer and a close pair').strings(unison=1.4, coupling=0.6).hammer(hardness=0.4).mics('Close Stereo', width=0.6).room('Chamber', 0.2, 0.5, 0.55),
    jz.copy('Latin Bright Grand', 'Bright, percussive, tight').set(instrument=INSTRUMENTS.index('Bright Pop Grand')).hammer(hardness=0.6).strings(decay=0.75).dampers(efficiency=1.6).room('Room', 0.1, 0.35).mics('Close Stereo'),
    jz.copy('Gospel Grand', 'Big, warm, rolling bass').hammer(hardness=0.55, mass=1.2).board(size=1.15, resonance=0.6).tone(bass=2).strings(coupling=0.6).room('Chamber', 0.2, 0.55),
    jz.copy('Quiet Standards', 'For pp playing: wide dynamics, soft curve').touch(dynamics=0.75, curve=-0.35).hammer(hardness=0.4).room('Chamber', 0.24, 0.55, 0.55),
    jz.copy('Fusion Studio Grand', 'Tight, bright, modern').set(instrument=INSTRUMENTS.index('Studio Grand')).hammer(hardness=0.6, felt=1.2).board(brightness=0.2).room('Room', 0.09, 0.35).mics('Close Stereo'),
])

# ---- Pop -------------------------------------------------------------------------------------
pp = P('Pop Grand', 'Bright Pop Grand', 'The bright studio grand that cuts through a mix').mics('Close Stereo').room('Room', 0.1, 0.4)
category('Pop', [
    pp,
    pp.copy('Pop Ballad', 'A touch softer with a plate-like chamber and longer decay').hammer(hardness=0.5).room('Chamber', 0.24, 0.6, 0.45, 18).board(resonance=0.55).strings(decay=1.2).touch(dynamics=0.6),
    pp.copy('Radio Piano', 'Compressed-feeling: narrow dynamics, bright').touch(dynamics=0.3, curve=0.3).hammer(hardness=0.65).tone(treble=2).room('Room', 0.12, 0.4),
    pp.copy('Singer-Songwriter Upright', 'An upright in a living room').set(instrument=INSTRUMENTS.index('Upright')).room('Room', 0.14, 0.4, 0.55).noises(master=0.7).mics('Player'),
    pp.copy('Stadium Grand', 'Huge and bright with a long hall').hammer(hardness=0.7, felt=1.6).board(size=1.15, resonance=0.6).mics('Wide Stereo').room('Hall', 0.35, 0.85, 0.4, 28),
    pp.copy('Lo-Fi Bedroom', 'A tired upright, dull and close').set(instrument=INSTRUMENTS.index('Vintage Upright')).tone(treble=-5, bass=1).hammer(hardness=0.3).room('Room', 0.08, 0.3, 0.8).mics('Mono').noises(master=0.9),
    pp.copy('Pop Chords Dry', 'Fast dampers, dry, for stabs').strings(decay=0.7).dampers(efficiency=1.8).room('Room', 0.05, 0.3).hammer(hardness=0.62),
    pp.copy('Glossy Pop Grand', 'Wide, shiny, modern').board(brightness=0.4, resonance=0.5).hammer(hardness=0.7).mics('Wide Stereo', width=1.0).room('Chamber', 0.2, 0.55, 0.35, 15).tone(treble=1),
    pp.copy('Indie Felt', 'The felt piano on an indie record: thinner felt, quicker, a little unison chorus').set(instrument=INSTRUMENTS.index('Felt Piano')).noises(master=0.8).room('Room', 0.2, 0.5, 0.6).mics('Close Stereo').hammer(hardness=0.58).strings(decay=0.85, unison=1.5),
    pp.copy('Piano Pop Anthem', 'Big hammers, long decay, hall').hammer(mass=1.4, hardness=0.6).strings(decay=1.3).room('Hall', 0.3, 0.75, 0.45, 22).mics('Wide Stereo'),
    pp.copy('Soft Verse Grand', 'For the quiet verse before the chorus').hammer(hardness=0.4, felt=0.7).touch(dynamics=0.6).board(brightness=-0.15).room('Chamber', 0.22, 0.55, 0.5),
    pp.copy('Motown Upright', 'A punchy upright with quick dampers').set(instrument=INSTRUMENTS.index('Upright')).hammer(hardness=0.62).strings(decay=0.75).dampers(efficiency=1.5).room('Room', 0.1, 0.35).mics('Mono').tone(bass=1.5),
    pp.copy('Dream Pop Wash', 'Soft felt and a long shimmering hall').set(instrument=INSTRUMENTS.index('Felt Piano')).hammer(hardness=0.4).board(sympathetic=0.8, resonance=0.7).room('Hall', 0.5, 1.0, 0.5, 45).mics('Wide Stereo'),
    pp.copy('Synth-Pop Clean', 'Very clean and even: low inharmonicity, tight unisons').strings(inharm=0.6, unison=0.4, coupling=0.3).hammer(hardness=0.6).noises(master=0.2).room('Room', 0.1, 0.4),
    pp.copy('Country Upright', 'A bright honky upright, lightly detuned').set(instrument=INSTRUMENTS.index('Upright')).strings(unison=2.5).hammer(hardness=0.65).room('Room', 0.12, 0.4).noises(master=0.7),
    pp.copy('Piano and Strings Bed', 'Soft, wide, with early reflections for a lush bed').hammer(hardness=0.42).mics('Custom', distance=2.5, spread=0.9, er=0.6).room('Hall', 0.4, 0.9, 0.5, 30).board(resonance=0.6),
])

# ---- Rock -----------------------------------------------------------------------------------
rk = P('Rock Grand', 'Bright Pop Grand', 'Hard hammers, tight dampers, close mics').hammer(hardness=0.75, felt=1.8, exponent=0.3).dampers(efficiency=1.5).mics('Close Stereo').room('Room', 0.08, 0.35)
category('Rock', [
    rk,
    rk.copy('Honky-Tonk Saloon', 'The classic saloon piano with wide, uneven unisons').set(instrument=INSTRUMENTS.index('Honky-Tonk')).hammer(hardness=0.6).room('Room', 0.15, 0.45).noises(master=0.8),
    rk.copy('Rock Upright', 'An upright miked for a band').set(instrument=INSTRUMENTS.index('Upright')).hammer(hardness=0.7).room('Room', 0.1, 0.4).tone(bass=2).noises(master=0.7),
    rk.copy('Pub Piano', 'Tired, out of tune, loud').set(instrument=INSTRUMENTS.index('Vintage Upright')).strings(unison=4.0).hammer(hardness=0.6).room('Room', 0.14, 0.4).noises(master=1.0).mics('Mono'),
    rk.copy('Arena Rock Grand', 'Bright grand, stadium reverb').board(size=1.1, brightness=0.3).mics('Wide Stereo').room('Hall', 0.3, 0.8, 0.4, 25),
    rk.copy('Garage Upright', 'Rough, short decay, lots of mechanics').set(instrument=INSTRUMENTS.index('Upright')).strings(decay=0.65, unison=2.8).hammer(hardness=0.7, felt=1.4).noises(master=1.0, hammer=1.0).room('Room', 0.1, 0.3, 0.7),
    rk.copy('Boogie Piano', 'Bright and bouncy with fast dampers').strings(decay=0.75, unison=1.8).dampers(efficiency=2.0).hammer(hardness=0.7).room('Room', 0.12, 0.4),
    rk.copy('Glam Grand', 'Over-the-top bright with a plate-ish chamber').hammer(hardness=0.8, felt=2.2, exponent=0.4).tone(treble=3).room('Chamber', 0.25, 0.6, 0.3, 10).mics('Wide Stereo'),
    rk.copy('Power Ballad', 'Big hammers, big hall').hammer(mass=1.5, hardness=0.65).strings(decay=1.2).room('Hall', 0.35, 0.85, 0.45, 30).mics('Wide Stereo'),
    rk.copy('Punk Upright', 'Harsh, short, dry').set(instrument=INSTRUMENTS.index('Upright')).hammer(hardness=0.85, felt=2.5, exponent=0.5).strings(decay=0.6).room('Room', 0.05, 0.3).mics('Mono'),
    rk.copy('Blues Barrelhouse', 'Detuned, worn, dark room').set(instrument=INSTRUMENTS.index('Honky-Tonk')).hammer(hardness=0.5).strings(unison=3.5).tone(treble=-2).room('Room', 0.18, 0.5, 0.7).noises(master=0.9),
    rk.copy('Prog Grand', 'Clean, wide, articulate').set(instrument=INSTRUMENTS.index('Studio Grand')).hammer(hardness=0.62).strings(inharm=0.85, unison=0.6).mics('Wide Stereo').room('Chamber', 0.2, 0.55),
    rk.copy('Tack Piano', 'Tacks in the hammers: hard, metallic strike').set(instrument=INSTRUMENTS.index('Honky-Tonk')).hammer(hardness=0.95, felt=3.5, exponent=0.8, mass=0.8).strings(decay=0.7).noises(master=0.8, hammer=1.0).room('Room', 0.12, 0.4),
    rk.copy('Grunge Felt', 'A dark felted upright played hard').set(instrument=INSTRUMENTS.index('Felt Piano')).hammer(hardness=0.6, felt=1.5).touch(curve=0.3).room('Room', 0.12, 0.4, 0.7).noises(master=0.9).mics('Close Stereo'),
    rk.copy('Live Rock Grand', 'From the FOH position: distant and loud, lid up').mics('Concert Audience').room('Stage', 0.3, 0.7, 0.4, 18).hammer(hardness=0.7).strings(decay=1.1, coupling=0.65).board(resonance=0.6),
])

# ---- Cinematic -----------------------------------------------------------------------------
ci = P('Cinematic Grand', 'Concert Grand', 'Soft felt, lots of sympathetic bloom, a scoring-stage hall').hammer(hardness=0.38, felt=0.65).board(sympathetic=0.85, resonance=0.7).mics('Wide Stereo').room('Hall', 0.4, 0.95, 0.45, 35)
category('Cinematic', [
    ci,
    ci.copy('Scoring Stage', 'A big stage, mics far back, heavier hammers').mics('Concert Audience', width=0.9).room('Hall', 0.45, 1.0, 0.5, 45).board(resonance=0.65).hammer(hardness=0.45, mass=1.15),
    ci.copy('Trailer Piano', 'Hard, dark, low, with a huge tail').hammer(hardness=0.6, mass=1.6).tone(bass=4, treble=-2).board(size=1.3, resonance=0.75).strings(decay=1.5).room('Hall', 0.5, 1.0, 0.35, 50),
    ci.copy('Intimate Score Felt', 'Felt piano very close, a little hall').set(instrument=INSTRUMENTS.index('Felt Piano')).mics('Close Stereo').room('Hall', 0.3, 0.8, 0.6, 30).noises(master=0.9),
    ci.copy('Dark Drone Grand', 'Experimental grand with endless decay and resonance').set(instrument=INSTRUMENTS.index('Experimental Grand')).strings(decay=2.5).board(sympathetic=1.0, resonance=0.85).room('Hall', 0.55, 1.0, 0.3, 60),
    ci.copy('Thriller Prepared', 'Prepared piano in a cold hall').set(instrument=INSTRUMENTS.index('Prepared Piano')).hammer(hardness=0.6).room('Hall', 0.4, 0.9, 0.3, 40).noises(master=1.0).mics('Wide Stereo'),
    ci.copy('Emotional Close Grand', 'Close, warm, with soft early reflections').hammer(hardness=0.35).mics('Custom', distance=0.9, spread=0.7, er=0.5).room('Chamber', 0.3, 0.7, 0.6, 20).board(brightness=-0.2),
    ci.copy('Ice Palace', 'Bright hammers, long bright hall').hammer(hardness=0.68).board(brightness=0.35, resonance=0.7).strings(decay=1.4).room('Hall', 0.5, 1.0, 0.15, 55).mics('Wide Stereo'),
    ci.copy('Underwater Grand', 'Soft, dull, slow: like a piano heard through water').hammer(hardness=0.2, felt=0.35, mass=1.5).tone(treble=-8).board(brightness=-0.8).room('Hall', 0.5, 0.95, 0.9, 40).strings(decay=1.3),
    ci.copy('Heartbeat Upright', 'An upright with heavy pedal and hammer noise, for tension').set(instrument=INSTRUMENTS.index('Vintage Upright')).noises(master=1.0, pedal=1.0, hammer=1.0).hammer(hardness=0.4).room('Room', 0.3, 0.6, 0.6).mics('Close Stereo'),
    ci.copy('Epic Low Octaves', 'Very large board and strong bass coupling').board(size=1.4, resonance=0.75).strings(coupling=0.9, decay=1.6).tone(bass=3).hammer(mass=1.4).room('Hall', 0.45, 1.0, 0.4, 45),
    ci.copy('Glass Harmonics', 'Strike point near the middle for hollow, odd-harmonic tones').set(instrument=INSTRUMENTS.index('Experimental Grand')).hammer(position=1.4, hardness=0.55).strings(inharm=0.5).room('Hall', 0.45, 0.95, 0.3, 40),
    ci.copy('Faraway Upright', 'A distant upright at the end of a corridor').set(instrument=INSTRUMENTS.index('Upright')).mics('Custom', distance=7.0, spread=0.4, er=0.7).room('Hall', 0.5, 0.9, 0.6, 60).tone(treble=-3),
    ci.copy('Suspense Mutes', 'Heavily damped strings, dry, nervous').strings(decay=0.4).dampers(efficiency=3.0).hammer(hardness=0.55).room('Room', 0.15, 0.4, 0.7).noises(master=0.8),
    ci.copy('Nostalgia Vintage', 'Vintage upright with a warm long hall').set(instrument=INSTRUMENTS.index('Vintage Upright')).hammer(hardness=0.35).room('Hall', 0.4, 0.85, 0.65, 30).tone(treble=-2).mics('Wide Stereo'),
    ci.copy('Minimalist Pulse', 'Even, clear, fast dampers for repeated figures').set(instrument=INSTRUMENTS.index('Studio Grand')).strings(unison=0.5, decay=0.8).dampers(efficiency=1.5).hammer(hardness=0.5).room('Chamber', 0.3, 0.7, 0.5, 20),
])

# ---- Intimate ---------------------------------------------------------------------------------
im = P('Intimate Grand', 'Dark Jazz Grand', 'Close, soft, every mechanical detail audible').hammer(hardness=0.38).noises(master=0.9, hammer=0.9, release=0.8, damper=0.8, pedal=0.8).mics('Close Stereo').room('Room', 0.12, 0.4, 0.6)
category('Intimate', [
    im,
    im.copy('Bedroom Upright', 'A small upright, lid closed, very close').set(instrument=INSTRUMENTS.index('Upright')).hammer(hardness=0.35).tone(treble=-2).room('Room', 0.08, 0.3, 0.7),
    im.copy('Whisper Felt', 'Felt piano at a whisper').set(instrument=INSTRUMENTS.index('Felt Piano')).touch(dynamics=0.4, curve=-0.4).noises(master=1.0).room('Room', 0.15, 0.45, 0.7),
    im.copy('Night Practice', 'Soft pedal engaged, dampers lazy').set(softPedalDepth=1.0).dampers(efficiency=0.6).hammer(hardness=0.3).room('Room', 0.1, 0.35, 0.7),
    im.copy('Living Room Grand', 'A home grand with the lid down').hammer(hardness=0.42).board(brightness=-0.3).mics('Player').room('Room', 0.14, 0.45, 0.55).noises(master=0.7),
    im.copy('Candlelight', 'Warm, slow, a little hall').hammer(hardness=0.32, felt=0.55).strings(decay=1.2).board(sympathetic=0.7).room('Chamber', 0.3, 0.65, 0.65, 18),
    im.copy('Soft Stretch Study', 'Light stretch, equal, honest for study').tuning(stretch=0.3).noises(master=0.6).room('Room', 0.08, 0.3).mics('Player'),
    im.copy('Close Mono Upright', 'One mic inside an upright, medium hammers').set(instrument=INSTRUMENTS.index('Upright')).mics('Mono').room('Room', 0.06, 0.3).noises(master=0.9).tone(bass=1).hammer(hardness=0.5).strings(decay=0.9),
    im.copy('Rainy Window', 'Felt, slow decay, soft reflections').set(instrument=INSTRUMENTS.index('Felt Piano')).strings(decay=1.3).mics('Custom', distance=0.6, spread=0.6, er=0.5).room('Chamber', 0.3, 0.6, 0.7, 25),
    im.copy('Lullaby Grand', 'Narrow dynamics, soft curve, warm').touch(dynamics=0.35, curve=-0.5).hammer(hardness=0.33).board(brightness=-0.2).room('Chamber', 0.24, 0.55, 0.6),
    im.copy('Hushed Hall', 'Soft playing in an empty hall').hammer(hardness=0.36).mics('Concert Audience').room('Hall', 0.4, 0.9, 0.55, 35).board(resonance=0.6),
    im.copy('Journal Upright', 'A warm old upright for sketching').set(instrument=INSTRUMENTS.index('Vintage Upright')).hammer(hardness=0.38).strings(unison=1.8).room('Room', 0.12, 0.4, 0.65).noises(master=0.8),
    im.copy('Thin Walls', 'Dry, a bit boxy, close').board(size=0.8, damping=1.6, resonance=0.35).tone(bass=-2).room('Room', 0.05, 0.25).mics('Close Stereo'),
    im.copy('Breath Felt Mono', 'Felt piano, mono, breathing mechanics').set(instrument=INSTRUMENTS.index('Felt Piano')).mics('Mono').noises(master=1.0).room('Room', 0.1, 0.35, 0.8),
    im.copy('Morning Light', 'Gentle, clear, small room').set(instrument=INSTRUMENTS.index('Studio Grand')).hammer(hardness=0.45).board(brightness=0.1).room('Room', 0.14, 0.45, 0.5).mics('Player'),
])

# ---- Felt --------------------------------------------------------------------------------------
ft = P('Felt Piano', 'Felt Piano', 'The felt strip down: soft, close, mechanical').mics('Close Stereo').room('Room', 0.18, 0.5, 0.6).noises(master=0.8)
category('Felt', [
    ft,
    ft.copy('Felt Grand Hall', 'Felted hammers on a grand in a hall').room('Hall', 0.4, 0.9, 0.5, 35).mics('Wide Stereo').board(resonance=0.65),
    ft.copy('Felt Dark', 'Thick felt: almost no high partials').hammer(hardness=0.2, felt=0.4).tone(treble=-4).strings(stiffness=2.0).board(brightness=-0.5).room('Room', 0.2, 0.5, 0.75),
    ft.copy('Felt Bright', 'Thin felt, more definition').hammer(hardness=0.7, felt=2.0).strings(stiffness=0.7).board(brightness=0.2).room('Room', 0.15, 0.45),
    ft.copy('Felt Pedal Wash', 'Lazy dampers and a huge pedal halo').dampers(efficiency=0.5).board(sympathetic=1.0, resonance=0.8).room('Hall', 0.45, 1.0, 0.55, 40),
    ft.copy('Felt Upright', 'The felt strip on an upright').set(instrument=INSTRUMENTS.index('Upright')).hammer(hardness=0.25, felt=0.3, exponent=-0.5).strings(stiffness=1.8).noises(master=1.0).room('Room', 0.15, 0.4, 0.7),
    ft.copy('Felt Tape', 'Dull, mono, old tape').mics('Mono').tone(treble=-6, bass=2).room('Room', 0.12, 0.4, 0.8).noises(master=0.9),
    ft.copy('Felt Wide Dream', 'Wide, slow, long reverb').strings(decay=1.4).mics('Wide Stereo', width=1.0).room('Hall', 0.5, 1.0, 0.6, 50),
    ft.copy('Felt Rhythmic', 'Quick dampers for pulsing figures').strings(decay=0.7).dampers(efficiency=2.0).touch(curve=0.2).room('Room', 0.1, 0.4),
    ft.copy('Felt Detuned', 'Slightly sour unisons').strings(unison=3.0, coupling=0.6).room('Room', 0.18, 0.5, 0.6),
    ft.copy('Felt Heavy Hammers', 'Heavy hammers through the felt: thuddy attacks').hammer(mass=1.8, hardness=0.4).noises(master=1.0, hammer=1.0).room('Room', 0.15, 0.45, 0.7),
    ft.copy('Felt Chamber', 'A felt piano in a small chamber, softer felt, livelier board').room('Chamber', 0.3, 0.6, 0.55, 15).mics('Player').hammer(hardness=0.42, felt=0.8).board(resonance=0.6),
    ft.copy('Felt Low Register', 'Big board for the low end, dark top').board(size=1.3, resonance=0.65).tone(bass=3, treble=-3).room('Hall', 0.35, 0.8, 0.6, 30),
    ft.copy('Felt Quiet Keys', 'Narrow dynamics, every key noise').touch(dynamics=0.3).noises(master=1.0, release=1.0, damper=1.0).room('Room', 0.12, 0.4, 0.7),
    ft.copy('Felt Meantone', 'Felt piano in meantone, for old tunes').tuning('Meantone 1/4', 0, 440, 0.2).room('Chamber', 0.25, 0.55, 0.6),
])

# ---- Uprights -----------------------------------------------------------------------------
up = P('Upright', 'Upright', 'A 1.2 m upright, player perspective').mics('Player').room('Room', 0.12, 0.4).noises(master=0.6)
category('Uprights', [
    up,
    up.copy('Upright Close', 'Mics right at the hammers').mics('Close Stereo').room('Room', 0.08, 0.35).noises(master=0.75).board(brightness=0.1),
    up.copy('Upright Bright', 'Hard-voiced upright').hammer(hardness=0.7, felt=1.5).board(brightness=0.25).room('Room', 0.1, 0.4),
    up.copy('Upright Mellow', 'Needled hammers, soft top').hammer(hardness=0.35, felt=0.65).board(brightness=-0.3).tone(treble=-1.5).room('Room', 0.14, 0.45, 0.6),
    up.copy('School Upright', 'A worn school piano with uneven unisons').set(instrument=INSTRUMENTS.index('Vintage Upright')).strings(unison=2.5).hammer(hardness=0.5).room('Room', 0.15, 0.45, 0.55).noises(master=0.8),
    up.copy('Upright Hall', 'An upright on a concert stage, singing a little longer').mics('Concert Audience').room('Hall', 0.32, 0.75, 0.5, 25).board(resonance=0.6, sympathetic=0.65).strings(decay=1.15),
    up.copy('Upright Studio', 'A well-regulated studio upright, tight and clean').strings(unison=0.7, decay=0.9).hammer(hardness=0.55).dampers(efficiency=1.3).room('Room', 0.1, 0.35).mics('Close Stereo'),
    up.copy('Upright Tall', 'A taller upright: longer bass strings, bigger board').board(size=1.2, resonance=0.6).strings(inharm=0.8, decay=1.15).tone(bass=1.5).room('Room', 0.14, 0.45),
    up.copy('Upright Spinet', 'A tiny spinet: short strings, high inharmonicity').board(size=0.75, damping=1.4, resonance=0.4).strings(inharm=1.8, decay=0.75).hammer(mass=0.8).room('Room', 0.1, 0.35),
    up.copy('Upright Pedal Halo', 'Lazy dampers and strong sympathetic ring').dampers(efficiency=0.6).board(sympathetic=0.85).room('Room', 0.18, 0.5, 0.55),
    up.copy('Upright Dry Mono', 'A single mic, no room, a well-regulated action').mics('Mono').room('Room', 0.03, 0.3).noises(master=0.7).hammer(hardness=0.56).strings(unison=0.8, decay=0.9),
    up.copy('Upright Worn Felt', 'Grooved, hard-packed felt').hammer(hardness=0.65, felt=1.3, exponent=0.3).strings(stiffness=1.3).noises(master=0.8, hammer=0.9).room('Room', 0.12, 0.4),
    up.copy('Upright Kirnberger', 'An upright in Kirnberger III').tuning('Kirnberger III', 0, 440, 0.5).hammer(hardness=0.48).room('Room', 0.15, 0.45, 0.5),
    up.copy('Upright Living Room', 'Lid closed, carpet, curtains').board(brightness=-0.2).mics('Custom', distance=1.5, spread=0.5, er=0.25).room('Room', 0.16, 0.45, 0.7).tone(treble=-1),
    up.copy('Upright Jazz Bar', 'Slightly detuned, warm, late').strings(unison=1.9).hammer(hardness=0.42).room('Room', 0.14, 0.45, 0.6).noises(master=0.7),
    up.copy('Upright Practice', 'Dry, neutral, honest').room('Room', 0.06, 0.3).tuning(stretch=0.5).noises(master=0.5),
])

# ---- Vintage ----------------------------------------------------------------------------------
vn = P('Vintage Upright', 'Vintage Upright', 'An old upright: worn hammers, uneven unisons, noisy action').mics('Player').room('Room', 0.14, 0.45, 0.6).noises(master=0.8)
category('Vintage', [
    vn,
    vn.copy('Vintage Saloon', 'Honky-tonk unisons on the vintage upright').strings(unison=4.5).hammer(hardness=0.6).room('Room', 0.15, 0.45).noises(master=0.9),
    vn.copy('Vintage Parlour Grand', 'A 1900s parlour grand').set(instrument=INSTRUMENTS.index('Studio Grand')).strings(unison=2.0, inharm=1.25, decay=0.8).hammer(hardness=0.42, mass=0.85).board(damping=1.3, size=0.9).room('Chamber', 0.22, 0.5, 0.6).noises(master=0.7),
    vn.copy('Vintage 78rpm', 'Narrow band, mono, hiss-free but dull').mics('Mono').tone(bass=-4, treble=-7).hammer(hardness=0.55).room('Room', 0.1, 0.35, 0.85).noises(master=0.6),
    vn.copy('Vintage Silent Film', 'A hard-worked cinema upright').strings(unison=3.5, decay=0.7).hammer(hardness=0.7, felt=1.5).dampers(efficiency=0.8).room('Hall', 0.25, 0.6, 0.5, 20).noises(master=1.0),
    vn.copy('Vintage Music Hall', 'The grand in a Victorian music hall').set(instrument=INSTRUMENTS.index('Concert Grand')).strings(unison=1.8, decay=0.85).hammer(hardness=0.45, mass=0.9).mics('Concert Audience').room('Hall', 0.3, 0.7, 0.55, 22),
    vn.copy('Vintage Dusty', 'Dull hammers, heavy damping, a lot of mechanics').hammer(hardness=0.28, felt=0.45).strings(stiffness=1.8).dampers(efficiency=1.4).noises(master=1.0).room('Room', 0.1, 0.35, 0.75),
    vn.copy('Vintage Ragtime', 'Bright and bouncy, detuned').strings(unison=3.0, decay=0.75).hammer(hardness=0.68, felt=1.4).dampers(efficiency=1.6).room('Room', 0.12, 0.4).tone(treble=1),
    vn.copy('Vintage Tango', 'Hard felt, dry, tight dampers, slightly flat pitch').hammer(hardness=0.7).tuning(pitch=435).dampers(efficiency=1.8).strings(decay=0.7).room('Room', 0.08, 0.3).mics('Mono'),
    vn.copy('Vintage Church Hall', 'An old upright in a wooden hall that rings along').room('Hall', 0.35, 0.8, 0.6, 30).hammer(hardness=0.4).mics('Wide Stereo').strings(decay=1.2, coupling=0.6).board(sympathetic=0.65),
    vn.copy('Vintage Prepared', 'Objects left on the strings of a vintage upright').set(instrument=INSTRUMENTS.index('Prepared Piano')).hammer(hardness=0.45).strings(unison=3.0).room('Room', 0.15, 0.45, 0.6).noises(master=1.0),
    vn.copy('Vintage Warm Grand', 'A dark, well-loved grand').set(instrument=INSTRUMENTS.index('Dark Jazz Grand')).strings(unison=1.6, decay=1.1).hammer(hardness=0.36).room('Chamber', 0.24, 0.55, 0.6).noises(master=0.7),
    vn.copy('Vintage Toy-Like', 'Tiny board, short strings, bright hammers').board(size=0.7, damping=2.0, resonance=0.35).strings(inharm=2.5, decay=0.5).hammer(hardness=0.75, mass=0.6).room('Room', 0.1, 0.35),
    vn.copy('Vintage Radio Hour', 'Mono, mid-forward, a little room, unisons drifting').mics('Mono').tone(bass=-2, treble=-3).room('Room', 0.18, 0.45, 0.6).hammer(hardness=0.5).strings(unison=2.2, decay=0.85).dampers(efficiency=1.3),
    vn.copy('Vintage Cabaret', 'Wide, detuned, bright, with stage reverb').strings(unison=3.8).hammer(hardness=0.62).mics('Wide Stereo').room('Stage', 0.3, 0.65, 0.5, 18).noises(master=0.8),
])

# ---- Ambient ---------------------------------------------------------------------------------
am = P('Ambient Grand', 'Concert Grand', 'Soft hammers, endless sustain, a cavernous hall').hammer(hardness=0.3, felt=0.5).strings(decay=2.0).board(sympathetic=0.9, resonance=0.8).dampers(efficiency=0.5).mics('Wide Stereo').room('Hall', 0.6, 1.0, 0.5, 60)
category('Ambient', [
    am,
    am.copy('Ambient Felt Drift', 'Felt piano dissolving into reverb').set(instrument=INSTRUMENTS.index('Felt Piano')).room('Hall', 0.7, 1.0, 0.6, 80).strings(decay=1.8),
    am.copy('Ambient Glass', 'Bright, bell-like, long').hammer(hardness=0.7, felt=1.6).strings(inharm=0.6, decay=2.5).board(brightness=0.3, resonance=0.75).room('Hall', 0.6, 1.0, 0.2, 60),
    am.copy('Ambient Deep', 'Huge board, strong bass, slow dark hall').set(instrument=INSTRUMENTS.index('Experimental Grand')).board(size=1.4, resonance=0.85).tone(bass=3, treble=-3).room('Hall', 0.65, 1.0, 0.7, 70),
    am.copy('Ambient Mutes', 'Prepared strings, soft hammers, long room').set(instrument=INSTRUMENTS.index('Prepared Piano')).hammer(hardness=0.3).room('Hall', 0.55, 1.0, 0.5, 50).strings(decay=1.5),
    am.copy('Ambient Pedal Cloud', 'Sympathetic bank at maximum: every note wakes the others').board(sympathetic=1.0, resonance=0.85).strings(coupling=0.9, decay=2.0).dampers(efficiency=0.4).room('Hall', 0.5, 1.0, 0.5, 45),
    am.copy('Ambient Upright Haze', 'Vintage upright washed out').set(instrument=INSTRUMENTS.index('Vintage Upright')).hammer(hardness=0.3).room('Hall', 0.65, 1.0, 0.65, 60).tone(treble=-3),
    am.copy('Ambient Pure Fifths', 'Just intonation and long decay for drones').tuning('Just Intonation', 0, 440, 0.0).strings(decay=2.5).board(sympathetic=0.9).room('Hall', 0.55, 1.0, 0.4, 55),
    am.copy('Ambient Still Water', 'Soft, dark, barely moving').hammer(hardness=0.22, felt=0.4).touch(dynamics=0.4).board(brightness=-0.5).room('Hall', 0.6, 1.0, 0.75, 70),
    am.copy('Ambient Shimmer Hall', 'Bright hall, bright hammers, soft felt exponent').hammer(hardness=0.6, exponent=-0.4).board(brightness=0.4).room('Hall', 0.6, 1.0, 0.1, 65).mics('Wide Stereo', width=1.0),
    am.copy('Ambient Distant Grand', 'Very far mics in an enormous space, the softest hammers').mics('Custom', distance=8.0, spread=0.6, er=0.8).room('Hall', 0.7, 1.0, 0.55, 90).strings(decay=2.6).board(resonance=0.85).hammer(hardness=0.26),
    am.copy('Ambient Felt Low', 'Low felt tones with a big board').set(instrument=INSTRUMENTS.index('Felt Piano')).board(size=1.35, resonance=0.8).tone(bass=4).room('Hall', 0.6, 1.0, 0.6, 60),
    am.copy('Ambient Music Box', 'Tiny bright strings on a long, sparkling hall').set(instrument=INSTRUMENTS.index('Studio Grand')).hammer(hardness=0.8, felt=2.5, mass=0.6).strings(inharm=2.0, decay=1.5).board(size=0.75, brightness=0.4).room('Hall', 0.55, 1.0, 0.15, 50),
    am.copy('Ambient Slow Dampers', 'Dampers that take seconds to settle').dampers(efficiency=0.25).strings(decay=1.6).room('Hall', 0.5, 0.95, 0.5, 45),
    am.copy('Ambient Night Choir', 'Soft hall with strong sympathetic chords').board(sympathetic=1.0, resonance=0.8).strings(coupling=0.8).hammer(hardness=0.35).room('Hall', 0.55, 1.0, 0.45, 55).mics('Concert Audience'),
])

# ---- Experimental --------------------------------------------------------------------------
ex = P('Experimental Grand', 'Experimental Grand', 'Four-metre strings, almost no stiffness, a board that sings').mics('Wide Stereo').room('Hall', 0.3, 0.8, 0.4, 30)
category('Experimental', [
    ex,
    ex.copy('Prepared Piano', 'Objects on the strings: detuned partials, buzzes, odd losses').set(instrument=INSTRUMENTS.index('Prepared Piano')).room('Room', 0.15, 0.45).noises(master=0.9).mics('Close Stereo'),
    ex.copy('Prepared Bolts', 'Harder objects: more inharmonic, brighter strike').set(instrument=INSTRUMENTS.index('Prepared Piano')).strings(inharm=3.0).hammer(hardness=0.8, felt=2.5).room('Room', 0.12, 0.4).noises(master=1.0),
    ex.copy('Prepared Rubber', 'Soft mutes: short, thuddy, dark').set(instrument=INSTRUMENTS.index('Prepared Piano')).strings(decay=0.3).hammer(hardness=0.3, felt=0.5).tone(treble=-4).room('Room', 0.1, 0.35, 0.8).noises(master=1.0),
    ex.copy('Middle Strike', 'Hammer at the string centre: only odd partials').hammer(position=2.0, hardness=0.5).strings(inharm=0.4).room('Hall', 0.3, 0.7),
    ex.copy('Edge Strike', 'Hammer at the very end: thin, nasal, all partials').hammer(position=0.5, hardness=0.6).room('Room', 0.12, 0.4),
    ex.copy('No Stiffness', 'Perfectly harmonic strings').strings(inharm=0.25, stiffness=0.5).hammer(hardness=0.55).room('Hall', 0.3, 0.75),
    ex.copy('Maximum Stiffness', 'Four times the inharmonicity: bell-like upper partials').strings(inharm=4.0).hammer(hardness=0.6).room('Hall', 0.3, 0.7),
    ex.copy('Giant Board', 'The largest soundboard, maximum resonance').board(size=1.4, resonance=1.0, sympathetic=1.0).strings(coupling=1.0, decay=2.0).room('Hall', 0.4, 0.9, 0.4, 40),
    ex.copy('Dead Board', 'A tiny, heavily damped board: strings almost alone').board(size=0.7, damping=3.0, resonance=0.0, sympathetic=0.0).strings(coupling=0.0).room('Room', 0.05, 0.3),
    ex.copy('Wide Unisons', 'Six cents between strings: chorus-like').strings(unison=6.0, coupling=0.8).room('Hall', 0.3, 0.7),
    ex.copy('Hammer of Lead', 'Double-mass hammers, very soft felt').hammer(mass=2.0, hardness=0.2, felt=0.3, exponent=-0.8).noises(master=1.0, hammer=1.0).room('Room', 0.15, 0.45, 0.7),
    ex.copy('Hammer of Glass', 'Light, very hard hammers').hammer(mass=0.5, hardness=1.0, felt=4.0, exponent=1.0).room('Hall', 0.3, 0.7, 0.2),
    ex.copy('Quarter-Tone Experiment', 'Equal temperament stretched hard and pitched high').tuning('Equal', 0, 466, 1.0).strings(inharm=1.5).room('Hall', 0.3, 0.7),
    ex.copy('Reverse Dynamics', 'Harder playing gets duller: an inverted velocity curve feel').touch(curve=-1.0, dynamics=1.0).hammer(hardness=0.6).room('Room', 0.15, 0.45),
    ex.copy('Infinite Sustain', 'Decay times four times longer, no dampers to speak of').strings(decay=4.0).dampers(efficiency=0.25).board(sympathetic=1.0).room('Hall', 0.4, 0.9, 0.4, 40),
    ex.copy('Honky-Tonk Hell', 'Everything out of tune, bright and loud').set(instrument=INSTRUMENTS.index('Honky-Tonk')).strings(unison=6.0).hammer(hardness=0.9, felt=3.0).room('Room', 0.15, 0.45).noises(master=1.0),
    ex.copy('Bridge Only', 'Maximum bridge coupling for two-stage decays').strings(coupling=1.0, unison=2.0).board(resonance=0.5).room('Room', 0.1, 0.35),
])


def build():
    cats = []
    seen = []
    problems = []
    for name, presets in CATEGORIES:
        out = []
        for pr in presets:
            for other_name, other in seen:
                d = synth_distance(pr.p, other)
                if d < 0.08:
                    problems.append(f'preset "{pr.name}" is near-identical to "{other_name}" (distance {d:.3f})')
            seen.append((pr.name, pr.p))
            entry = {'name': pr.name, 'description': pr.desc, 'params': dict(sorted(pr.p.items()))}
            out.append(entry)
        cats.append({'name': name, 'presets': out})
    if problems:
        raise SystemExit(chr(10).join(problems))
    return {'format': 'Resonique Factory Presets', 'version': 1, 'categories': cats}


def apply_trims(data, csv_path):
    trims = {}
    with open(csv_path, newline='') as f:
        for row in csv.DictReader(f):
            try:
                level = float(row['loudest_100ms_dbfs']); peak = float(row['peak_dbfs'])
            except (KeyError, ValueError):
                continue
            trim = TARGET_DBFS - level
            trim = min(trim, -1.0 - peak)          # never push the peak past -1 dBFS
            trims[row['name']] = round(max(-24.0, min(12.0, trim)), 2)
    with open(TRIMS, 'w') as f:
        json.dump(trims, f, indent=1, sort_keys=True)
    return trims


def main():
    data = build()
    trims = {}
    if '--match' in sys.argv:
        trims = apply_trims(data, sys.argv[sys.argv.index('--match') + 1])
    elif os.path.exists(TRIMS):
        with open(TRIMS) as f:
            trims = json.load(f)
    n = 0
    for cat in data['categories']:
        for p in cat['presets']:
            if p['name'] in trims:
                p['params']['presetGain'] = trims[p['name']]
            n += 1
    with open(OUT, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)
    print(f'wrote {OUT}: {n} presets in {len(data["categories"])} categories')


if __name__ == '__main__':
    main()
