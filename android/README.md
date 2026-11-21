# Resonique for Android

The web build (`web/index.html`) packaged as a native Android app for Google Play, with **MIDI working on the
phone** and a layout made for phones in portrait and landscape.

## How it works

| Piece | What it does |
|---|---|
| `MainActivity.java` | A full-screen WebView. It serves the bundled page from `https://appassets.androidplatform.net/assets/index.html`, which counts as a secure context, so the engine runs in an **AudioWorklet** (its own audio thread) and presets persist in local storage. It hands the file picker to **IMPORT** and **LOAD .SCL**, writes **EXPORT** through the system "save as" dialog (`window.AndroidFiles`), and when the app leaves the screen it releases held notes and suspends audio, resuming it when the app returns. |
| `MidiBridge.java` | Native MIDI through `android.media.midi`. Android's WebView has **no Web MIDI**, so this opens every MIDI input device and splits the byte stream into messages (running status included; SysEx and real-time bytes dropped). It forwards messages to the page in batches and picks up devices plugged in or removed while the app runs. |
| `assets/android-midi.js` | Provides `navigator.requestMIDIAccess()` on top of `MidiBridge`, so the web host's MIDI code runs unchanged: notes, sustain / sostenuto / soft pedals, pitch bend, all notes off. |
| `bundleWebApp` (Gradle task) | Copies `../web/index.html` into the app's assets at build time and inserts the shim at the top of `<head>`. `build.bat` rebuilds the web page first. |

**The phone layout** lives in the shared UI (`vst/ui`: the `compact` classes in `style.css` and `fit()` in
`app.js`). The desktop and plugin layouts are unchanged; touch screens too small for the full panel switch to it.
- **Portrait:** one column 480 px wide with 25 keys.
- **Landscape:** panels two across on a 900 px width, with 37 short keys.
- **Keyboard:** always pinned to the bottom of the screen, with the three pedals and octave buttons; the panels
  scroll above it. Multitouch; touch lower on a key to play harder.
- **Preset browser:** folds away; tap the preset name to open it.

**MIDI devices that work**
- **USB MIDI keyboards and controllers** (class-compliant), through a USB-C or OTG adapter. No permission prompt
  is needed, and they appear as soon as they're plugged in.
- **Bluetooth LE MIDI** devices, once Android has connected them. Android has no built-in pairing screen for MIDI,
  so connect the device first with a free app such as *MIDI BLE Connect*. It then shows up under MIDI IN.

The app also installs on devices without MIDI.

## Build

```
build.bat           signed release:  app\build\outputs\bundle\release\app-release.aab  (Google Play)
                                      app\build\outputs\apk\release\app-release.apk     (sideloading)
build.bat debug     debug APK:       app\build\outputs\apk\debug\app-debug.apk
```

`build.bat` regenerates `web/index.html` (`vst\webhost\build-web.ps1`) and then runs Gradle 8.14 with Android
Gradle plugin 8.13 (Gradle 9 does not work with this plugin). It needs a JDK 17 or newer and the Android SDK
(API 36):
- **JDK:** set `JAVA_HOME`; otherwise it uses Android Studio's bundled JDK or `%ProgramFiles%\Android\openjdk`.
- **SDK:** set `sdk.dir` in `local.properties` (git-ignored), or set `ANDROID_HOME`.

Android Studio also works: *File → Open* → this `android/` folder (run `vst\webhost\build-web.ps1` first if you
changed the UI or the engine).

Install on a phone with USB debugging on:
```
adb install -r app\build\outputs\apk\release\app-release.apk
```
Debug builds let `chrome://inspect` (or Edge's `edge://inspect`) attach to the page; release builds don't.
`tests\check-device.js` uses that: it installs the debug APK, opens the app and reads the page's audio state and
layout over the DevTools protocol.

### Signing

The release is signed with the **upload key** `upload-key.jks`. Its passwords are in `keystore.properties`. Both
files are in this folder, git-ignored, and were generated for this project.

**Back them up somewhere safe (a password manager or an offline copy).** If the upload key is lost, Google can
reset it, but that takes a support request and some days.

To make a new key instead:
```
keytool -genkeypair -keystore upload-key.jks -storetype PKCS12 -alias upload -keyalg RSA -keysize 4096 -validity 10000
```
Then copy `keystore.properties.example` to `keystore.properties` and fill in the passwords.

## Publishing on Google Play

Everything for the store is in [`store/`](store/listing.md): the listing text, a 512 px icon, a 1024 × 500
feature graphic, four phone screenshots (three portrait, one landscape), the privacy policy, the data-safety and
content-rating answers, release notes and a pre-launch checklist. The policy pages that Play links to are
`docs/privacy.html` and `docs/terms.html` (GitHub Pages). `store\make-graphics.ps1` regenerates the graphics
from `graphics.html` and from the web build in its phone layout.

1. You need a [Play Console](https://play.google.com/console) developer account (one-time US$25). **New personal
   accounts** must run a closed test with at least 12 testers for 14 days before they can publish to production.
2. Create the app (Music & Audio, free) and upload `app-release.aab` to a testing track. Keep **Play App Signing**
   on (the default): Google holds the app signing key, and you sign uploads with the upload key above.
3. Fill in the store listing and the *App content* declarations from `store/listing.md`.
4. For every update, raise `versionCode` in `app/build.gradle`. `versionName` follows the plugin version in
   `vst/CMakeLists.txt`.

**What makes it Play-ready**
- **Target:** API 36, as Play requires. It runs from Android 8.0 (API 26) up.
- **Format:** an App Bundle (`.aab`), shrunk and optimized with R8; about 260 KB.
- **Permissions:** none at all, and no network access.
- **Hardware:** MIDI, USB host and touchscreen are all optional, so the app installs on any device.
- **Icon:** an adaptive icon (the grand piano) with a monochrome layer for Android 13+ themed icons.
- **Backup:** user presets are included in the user's Android backup.
- **Native code:** none (the engine is WebAssembly inside the page), so Play's 16 KB page-size requirement
  doesn't apply.

**Application ID:** `com.blinkzer0.resonique`. It can never change once the app is published, so edit
`applicationId` in `app/build.gradle` before the first upload if you want a different one.
