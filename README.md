# SLOOP Touch

A touch groovebox for Android, in the spirit of Ableton Note. Eight tracks of synths and drums, a session of clips
and scenes, live recording, a mixer, an audio track for samples, MIDI and Ableton Link. The engine is SLOOP's audio
engine (14 synthesis engines, 37 drum kits, effects, a sample-accurate sequencer). SLOOP was a groovebox firmware
for the M-VAVE FM-1; this repository is the app only.

> **Status:** in development, not released yet. CI builds a test APK from every push (see [Try it](#try-it)).
> The name is temporary.

## What it does

### Session
- 8 tracks: DR (drums) and T1–T7 (synths), plus **AU**, an audio track.
- Clips of 1 to 64 steps. Clips and scenes launch at the next bar. Two fingers on several clips chain them.
- Copy, paste, duplicate, double and quantize clips.
- Undo and redo.

### Editing
- A drum grid and a piano roll.
- The piano roll shows the degrees of the chosen key and scale (11 scales); changing the key moves the clips into
  it.
- Long notes, velocity, probability, ratchets, micro-timing.
- Step conditions: 1:2, 2:2, 1:4, Fill, Not fill.
- Swing.
- Parameter locks on any parameter: hold steps, turn a knob.

### Playing
- Drum pads (QFG and MPC layouts).
- A keyboard in fourths, in key, with chords and octave buttons.
- Velocity from where a pad is hit.
- Note repeat (1/4 to 1/16T), FILL.

### Recording
- Pads, the keyboard and MIDI record live. Note lengths are kept; hits can stay off the grid.
- Knob moves record as parameter locks.
- Count-in and metronome.

### Sound
- A sound browser by category and by engine.
- Every parameter on four knobs, page by page.
- A Juno-style chorus and a VB-2-style vibrato on every synth track.
- Engines: ANALOG, DIGITAL (FM-4), PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, FM6 (DX7-style), PHYS, NOISE,
  PLAITS (Mutable Instruments) and OBXD (trial).

### Audio track
- One sample per scene row: imported (WAV, AIFF, FLAC, Ogg, MP3) or one of the app's own recordings.
- Its start and end can be trimmed; the file itself is never changed.
- Its tempo can be set by hand or by tapping along. It plays on the song's beats, either time-stretched (pitch
  kept) or sped up like a record.

### Mixer
- Mute, solo, pan, reverb and delay sends, faders and meters per track.
- The master bus: filter, DUST, DUCK, volume.

### Out
- **● REC** records the master into a WAV and opens the share sheet.
- Export to [DAWproject](https://github.com/bitwig/dawproject), for Bitwig, Studio One or Cubase.
- Projects: `.sloop` files carry their samples with them.

### Sync
- **MIDI**: input over USB or Bluetooth (notes, CC to the knobs, start/stop), and MIDI clock out.
- **Ableton Link**: tempo, bar and play/stop with Live, Bitwig, Koala and other Link apps on the same Wi‑Fi.

## Try it

Each push builds an APK.

1. Open the latest run of the **App** workflow in the *Actions* tab.
2. Download the `sloop-touch-apk` artifact and install it on an Android phone (Android 8 or later).

## Build

Requirements:
- Desktop: CMake ≥ 3.22, Ninja, clang, Python 3, and on Linux the ALSA, X11, Freetype and Fontconfig development
  packages (see `.github/workflows/app.yml`).
- Android: the SDK with NDK 26.2.11394342 and CMake 3.22.1.

```sh
# desktop (Linux, macOS, Windows): the app, its unit tests
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
build/SloopTests_artefacts/Release/SloopTests                 # unit tests
"build/SloopTouch_artefacts/Release/SLOOP Touch" --selftest     # the audio side, end to end

# Android (JUCE's CMake does not do Android: Projucer writes a Gradle project)
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git app/JUCE
tools/gen_engine_tables.sh && tools/get_deps.sh
Projucer --resave app/SloopTouch.jucer
cd app/Builds/Android && ./gradlew assembleRelease
```

- The CMake build fetches JUCE 8.0.4 (unless you pass `-DJUCE_DIR=...` or have it in `app/JUCE`). It also fetches
  Ableton Link and Signalsmith Stretch (`tools/get_deps.sh`) and generates the engine's tables.
- The app also has `--snapshot out.png 844x390 session|grid|pads|mix|audio|...`, which draws a screen into an image.

## Repository

| Path | What |
| --- | --- |
| `app/` | The JUCE app (C++17): `Source/` (`model/`, `audio/`, `control/`, `ui/`) and `SloopTouch.jucer` (Android). |
| `engine/` | The engine as a C library (`sloop_engine.h`). `core/` holds SLOOP's DSP and sequencer; `host.c` builds them as one unit. Also the PLAITS and OBXD engines. |
| `tests/` | Unit tests (the scheduler, clips, undo, projects). |
| `third_party/` | Plaits and Obxd (vendored). Link and Signalsmith are fetched there. |
| `tools/` | The generators of the engine's tables, and the fetch script. |
| `assets/samples-cc0/` | The CC0 recordings the SAMPLE engine and the acoustic kit are made from. |
| `mockup/` | The HTML mockup the UI follows. |
| `docs/` | [ARCHITECTURE.md](docs/ARCHITECTURE.md) (how the code is organised) and [HISTORY.md](docs/HISTORY.md) (where it comes from, what changed). |

## Credits and licence

GPL-3.0-only (`LICENSE`). Third-party code and data are listed in [LICENSING.md](LICENSING.md).

The engine comes from [SLOOP](https://github.com/isod89/sloop-fm1) by isod89, itself built on
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments. Thanks also to:
- Mutable Instruments (Plaits), Vadim Filatov (Obxd), Ableton (Link), Signalsmith Audio (Stretch);
- msfa / Dexed (FM6), DaisySP and Rings (PHYS);
- Versilian Studios and Sonic Pi, for the CC0 samples.
