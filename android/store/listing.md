# Google Play store listing: Resonique

Everything Play Console asks for, in the order it asks. Paste the texts; upload the graphics from this folder.
The app bundle comes from `android\build.bat` (`app\build\outputs\bundle\release\app-release.aab`).

## Main store listing (Grow users → Store presence → Main store listing)

**App name** (30 max, 9 used): Resonique

**Short description** (80 max, 78 used):
Physically modelled grand piano: no samples, ten pianos, MIDI, Scala tuning

**Full description** (4000 max):

Resonique is a piano computed from physics, not played back from recordings. Every note is a stiff-string model struck by a nonlinear felt hammer, coupled through a modal soundboard that carries the resonance back into every undamped string. Play softly and the tone is round; dig in and the hammer contact shortens and the sound opens up, continuously, the way a real instrument does.

TEN PIANOS
A 2.7 m concert grand, a studio grand, an upright, a vintage upright, a bright pop grand, a dark jazz grand, a felt piano, a prepared piano, a honky-tonk and an experimental four-metre grand. 190 presets in twelve categories, from concert and studio to intimate, cinematic and experimental.

THE MODEL IS YOURS TO SHAPE
• Strings: inharmonicity, stiffness, decay, unison detune and bridge coupling.
• Hammer: strike point, mass, felt stiffness and exponent; a velocity curve you can drag.
• Soundboard and dampers: size, damping, brightness, damper efficiency.
• Sympathetic resonance and continuous half-pedalling, sostenuto and soft pedal.
• Mechanical noises synthesised, not sampled: hammers, key release, dampers, pedals.

MICROPHONES AND ROOMS
Five perspectives (player, close, audience, wide, mono) or custom distance, spread and position, early reflections, and four algorithmic rooms.

TUNING
Equal temperament and seven historical temperaments with any root, concert pitch from 380 to 500 Hz, Railsback stretch, Scala (.scl) files, and per-note detune.

MADE FOR PHONES AND TABLETS
Portrait or landscape, with a multitouch keyboard pinned to the bottom (touch lower on a key to play harder) and octave buttons. Save your own presets, and import or export the same .rqpreset files as the Resonique VST3 plugin for Windows.

MIDI IN
USB MIDI keyboards and controllers (USB-C or OTG adapter), and Bluetooth MIDI devices once Android has connected them. Sustain, sostenuto and soft pedals, pitch bend, all-notes-off.

No ads, no account, no internet connection needed, no data collected.

**App icon**: `icon-512.png` (512 × 512 PNG)
**Feature graphic**: `feature-1024x500.png` (1024 × 500 PNG)
**Phone screenshots** (1080 × 2160 portrait, 2160 × 1080 landscape; Play needs 2–8, long side at most twice the short side):
1. `screen-1-play.png` – MAIN page, playing (portrait)
2. `screen-2-physics.png` – PHYSICAL MODEL page (portrait)
3. `screen-3-mics.png` – MICROPHONES page (portrait)
4. `screen-4-landscape.png` – MAIN page (landscape)
(The same files serve as 7-inch and 10-inch tablet screenshots if Play asks for them.)

**Category**: Music & Audio · **Tags**: piano, synthesizer, MIDI, music production
**Contact details**: the developer email of the Play account; website `https://github.com/BlinkZer0/Resonique`
**Privacy policy URL**: `https://blinkzer0.github.io/Resonique/privacy.html` (docs/privacy.html; enable GitHub Pages for the `docs/` folder)

## Store settings

- App or game: **App** · Category: **Music & Audio** · Free (cannot be changed to paid later)
- Store listing contact email: required · External marketing: your choice

## App content (Policy → App content)

| Declaration | Answer |
|---|---|
| **Privacy policy** | the URL above |
| **Ads** | No, the app does not contain ads |
| **App access** | All functionality is available without special access (no login) |
| **Content rating** | Start the questionnaire, category *Utility, Productivity, Communication, or Other*: answer **No** to every question (no violence, sexuality, language, controlled substances, user interaction, sharing of location or personal info, purchases). Expected rating: Everyone / PEGI 3. |
| **Target audience and content** | Target age group: **18 and over** (simplest; the app is not designed for children). "Appeal to children": No. |
| **News app** | No |
| **COVID-19 contact tracing / status** | No |
| **Data safety** | see below |
| **Government app** | No |
| **Financial features** | None of the listed features |
| **Health** | None of the listed features |
| **Advertising ID** | The app does not use an advertising ID |

### Data safety

- Does your app collect or share any of the required user data types? **No**
- Is all of the user data collected by your app encrypted in transit? (not asked when nothing is collected)
- Do you provide a way for users to request that their data is deleted? (not asked)
Result shown on the listing: *No data collected · No data shared with third parties*.

Why it is true: the app declares no permissions and has no network access; the page is served from the APK; presets and settings stay in the app's local storage (included in the user's own Android backup, which Google treats as the user's data); MIDI data only drives the sound engine; files you import are read once; the export dialog writes where you choose.

## Release (Test and release)

1. Play App Signing: keep it on (default). The bundle is signed with the **upload key** in `android/upload-key.jks`; Google keeps the app signing key.
2. **Internal testing** first: upload `app-release.aab`, add testers by email, install from the opt-in link, play.
3. **Closed testing** (new personal developer accounts: at least 12 testers opted in for 14 days before production is unlocked).
4. **Production**: countries, then roll out. Release notes (500 max per language):

```
<en-US>
First release: the physically modelled Resonique piano on Android. Ten pianos, 190 presets, MIDI in (USB and Bluetooth), pedals, microphones and rooms, temperaments, Scala tuning and per-note detune; presets import and export as .rqpreset files shared with the VST3 plugin.
</en-US>
```

5. Every later upload: raise `versionCode` in `android/app/build.gradle` (versionName follows `vst/CMakeLists.txt`).

## Pre-launch checklist

- `android\build.bat` succeeded and `app-release.aab` is signed (`apksigner verify --print-certs app-release.apk` shows the upload certificate).
- The APK installed on a phone: audio runs (statusbar shows *AudioWorklet · 48.0 kHz*), touch keys play, a MIDI keyboard appears under MIDI IN, rotating keeps the state.
- The privacy policy URL loads (GitHub Pages enabled for `docs/`).
- Terms of use: `https://blinkzer0.github.io/Resonique/terms.html` (docs/terms.html; Play does not require it, but the listing's website link and the About text point to it).
