# SLOOP Touch: where it comes from, what changed, what was added

SLOOP Touch is a touch groovebox app (Android first) built on the audio engine of SLOOP, a groovebox firmware for
the M-VAVE FM-1. This document is the project's history: what the starting code base was, what we changed in it,
and what we added on top. For how the code is organised today, read [ARCHITECTURE.md](ARCHITECTURE.md).

## 1. The lineage

| Project | Author | What it is |
| --- | --- | --- |
| [Felucca](https://github.com/hugelton/Felucca) | Leo Kuroshita / Hügelton Instruments | An open firmware for the M-VAVE FM-1 mini keyboard: the first replacement firmware and its synthesis core. |
| [SLOOP](https://github.com/isod89/sloop-fm1) | isod89 | A live groovebox firmware built on Felucca. It has 3 synth tracks and a drum track, 12 synthesis engines, 37 drum kits, a 64-step sequencer with parameter locks, micro timing and fill conditions, effects, a song mode and MIDI. It also has a web editor whose audio preview runs the firmware's own DSP in the browser. |
| **SLOOP Touch** (this fork, `toitoinou/sloop-fm1`) | toitoinou, with Claude | A touch app in the spirit of Ableton Note, built on SLOOP's engine. |

The fork starts at **SLOOP 2.8.1**, commit `d893642` (2026-10-23). Everything after it is the work described here.
At first the app lived in `mobile/`, next to the FM-1 firmware. Since then the repository is **the app only**
(section 8): the paths in sections 2 to 6 are the ones of that time. The licence stays GPL-3.0.

## 2. The starting point (SLOOP 2.8.1)

What we inherited and kept:

- **The DSP and the sequencer, in portable C** (`firmware/src/`):
  - `core.h`: the data model (tracks, steps, parameters `P_*`, globals `G_*`, presets);
  - `seq.c`: the sequencer, the transport clock, recording, the MIDI clock;
  - `voice.c`: voices and their allocation;
  - `fx.c`: the effects and the mix bus;
  - `engines.c` and `eng_*.c`: the synthesis engines, each an `engine_t`;
  - `drums.c`: the drum kits;
  - `params.c`: parameter ranges, labels and pages.
- **`firmware/host/preview.c`**: the web editor's adapter. It compiles all of the above into one translation unit
  (a "unity build"). The hardware (keys, screen, flash, the second core) is replaced by RAM and stubs. It exposes a
  small `preview_*` API that renders blocks of 32 frames at 44.1 kHz. This is what made a mobile port possible
  without rewriting the engine.
- **The table generators** (`tools/gen_*.py`): wavetables, samples, drum kits and FM6 patches as C headers.

SLOOP's own model is the FM-1's:

- 4 tracks, each looping 64 steps;
- one sound per track;
- 8 "EDIT" parameters per engine;
- 24 parameter locks per track;
- the sequence stored in the engine.

## 3. What we changed in the firmware sources

The user explicitly allowed breaking compatibility with the FM-1. The FM-1 was the starting code base, not a
target. So these changes favour the app.

> These sources now live in `engine/core/` and are built for the app only (section 8); the FM-1 build is gone.

| File | Change | Why |
| --- | --- | --- |
| `core.h` | `NPART` (synth parts) can be overridden at build time; default 3, the app uses 7. `NTRK = NPART + 1`, `TRK_DRUM = NPART`. | Eight tracks in the app. |
| `core.h` | `VOICE_BUDGET`, the voices sounding at once over all tracks (the app: 32). | More tracks need more voices. |
| `core.h` | `NEDIT` 8 → 16: `P_E8..P_E15`, 4 EDIT pages per engine. `preset_t` gains `e2set` / `e2` with the `E2(...)` macro, for presets of the extra parameters. | Richer engines (ANALOG, OBXD). |
| `core.h`, `params.c`, `seq.c`, `fx.c` | A **MOD stage** per synth track: `P_MTYPE`, `P_MRATE`, `P_MDEPTH`, `P_MRISE`, placed before `P_E0` (so `P_E0` is now 67). Types: OFF, JUNO I, JUNO II, JUNO I+II, VIBRATO. `mod_run` in `fx.c` is a stereo BBD-style chorus / vibrato in each part's mix. The four parameters can be locked. | A Juno-60 chorus, and a Boss VB-2 style vibrato. |
| `core.h`, `engines.c` | `FELUCCA_PLAITS` and `FELUCCA_OBXD` flags add the app's two engines (their sources live in `mobile/engine`). | Off on the FM-1. |
| `eng_analog.c` | **Rewritten.** Two oscillators (each with its own wave), a sub, sync, noise, an LP24 TPT ladder filter with its own envelope, 4 pages (OSC, SRC, FLT, FENV), 32 presets. | The old ANALOG was thin. |
| `voice.c`, `seq.c` | Loops over `NEDIT`, `voices_busy()` against `VOICE_BUDGET`, the lock limits up to `P_E15`, `ML_E0` 110. | Follows the changes above. |
| `seq.c` | `ext_adv`: an outside clock. When set, the transport moves by what the app says this block. The MIDI clock-in path does the same thing. | Ableton Link. |
| `host/preview.c` | The panic clears every track (`(1 << NTRK) - 1`), not 4. | Eight tracks. |

Projects in the app's format record which parameter layout they use. Files from before the MOD stage are migrated
on load (ids ≥ 63 shift by 4; `Project.cpp`).

## 4. What we added

### 4.1 The engine as a library (`mobile/engine/`)

- **`sloop_engine.h`, `sloop_engine.c`**: a plain C API (`se_*`) around `preview.c`.
  - The calls cover: transport; sounds and their names; parameters and their descriptions; whole steps; locks per
    step; the mix (solo, meters, globals); live notes with velocity; the metronome; and the outside clock
    (`se_set_advance`).
  - Static asserts keep its ids in step with `core.h`.
- **PLAITS**: Mutable Instruments' Plaits (MIT), vendored in `third_party/plaits`.
  - Files: `eng_plaits.c` and `plaits_glue.cc`.
  - 24 models, 27 presets, 4 voices per part.
- **OBXD**: the voice of Vadim Filatov's Obxd, an Oberheim OB-X emulation, vendored in `third_party/obxd`.
  - Files: `eng_obxd.c` and `obxd_glue.cc`.
  - 4 pages, 12 presets, 6 voices per part.
  - **A trial: its licence must be cleared before a release** (see `third_party/obxd/README.md`).

### 4.2 The app (`mobile/app/`, JUCE 8, C++17)

**Session and clips**

- A session in the style of Ableton Note / Live: a column per track, a row per scene. Clips launch at the next
  bar, and so do scenes.
- Chains: two fingers on several clips of a track play them in turn. A stop button per track; "Stop clips".
- Clips of 1 to 64 steps. Each track loops its playlist whatever the other tracks' lengths.
- Copy, paste and duplicate clips. Double a clip; quantize it. Insert, capture, duplicate or delete scenes.
- Undo / redo of every session edit; edits close in time merge into one step.

**Editing**

- A drum grid (16 lanes) and a piano roll for the synth tracks (24 rows: the degrees of the key's scale).
- Long notes, velocity, probability, ratchets, nudge (micro timing), and conditions (1:2, 2:2, 1:4, Fill, Not
  fill). The step sheet opens with a long press.
- A key and scale (11 scales). Changing the key moves every clip into it.
- Swing, and parameter locks on any parameter: hold steps and turn a knob.

**Playing**

- Pads: a drum layout (QFG / MPC) and a keyboard in fourths, in key, with octave and chord buttons.
- Velocity from where the pad is hit.
- Note repeat (1/4 … 1/16T), on the tempo's grid.
- FILL, held.

**Sound**

- 8 tracks: DR plus T1–T7 (Bass, Keys, Lead, Pad, Pluck, Arp, Texture).
- A full-screen sound browser by category (Basses, Leads, Pads…) and by engine, with audition.
- Every parameter on 4 knobs, page by page (Mix, the engine's macros, its EDIT pages, envelope, MOD, FX).
- Quick knobs: a short swipe covers the whole range.

**Recording**

- Live recording from the pads, the keyboard and MIDI. Note lengths are kept; the nudge or the grid is used, as
  set.
- Knob moves are recorded as parameter locks; "Effacer les mouvements" clears them.
- Count-in and metronome.

**Mixer**

- A strip per track: mute, solo, pan, reverb and delay sends, fader, meter.
- The drum track's level and sends are SLOOP's globals.
- The master strip: SLOOP's DJ filter, DUST and DUCK on the bus, the volume.

**Audio out**

- **● REC** on the master records what is heard into a 24-bit stereo WAV, then opens the phone's share sheet.
- **DAWproject export** (Bitwig, Studio One, Cubase…):
  - one MIDI track per track;
  - the scenes in the DAW's clip launcher, and laid one after the other in the arrangement;
  - the audio clips, warped, with their samples inside the file;
  - checked against the format's XSD schema.

**Sync and MIDI**

- MIDI in from USB and Bluetooth (pairing from Réglages). Notes go to the selected track and are recorded.
  CC 21–24 / 70–73 turn the knobs. Start and Stop are followed.
- MIDI clock out.
- **Ableton Link**: tempo, bar phase and Play / Stop shared with the other Link apps on the network. On Android a
  Wi‑Fi multicast lock is held while Link is on.

**The audio track (AU)**

- One sample per scene row: imported (WAV, AIFF, FLAC, Ogg, MP3) or taken from the app's recordings.
- Start and end are trimmed without touching the file.
- Its own tempo: set by hand or by tapping along while it plays alone. It is guessed from the file name or length
  at first.
- It plays that many beats of the song, at the song's tempo:
  - stretched, keeping its pitch (Signalsmith Stretch, MIT);
  - or sped up like a record.
- It is locked to the song's beat block by block, so Link and tempo changes move it too.
- It has its own mixer strip.

**Projects**

- Named projects and an autosave.
- Export and import as `.sloop` (JSON, format 3; older formats migrate on load).
- Settings live apart, in `prefs.json`.

**Tooling**

- `--selftest`: the audio side without a device; 46 checks.
- `--snapshot`: renders a screen to a PNG.
- `--dawproject`: writes the demo as a DAWproject file.
- CI (`.github/workflows/mobile.yml`) builds the APK, builds the desktop app, runs the self-test and uploads
  screenshots.

### 4.3 Other additions

- `mobile/mockup/`: a playable HTML mockup of the UI, the reference the screens were built from.
- `mobile/tools/gen_engine_tables.sh`: runs the firmware's generators for the app.
- `mobile/tools/get_deps.sh`: fetches the libraries kept out of the repository.

## 5. Third-party code

| Library | Where | Licence | Use |
| --- | --- | --- | --- |
| JUCE 8.0.4 | fetched (`mobile/app/JUCE`) | AGPLv3 or commercial (AGPLv3 combines with GPL-3) | UI, audio and MIDI devices, files, zip |
| Mutable Instruments Plaits + stmlib | vendored, `third_party/plaits` | MIT | the PLAITS engine |
| Obxd (2DaT) voice | vendored, `third_party/obxd` | GPL (2 or 3: **to clear**) | the OBXD engine (trial) |
| Ableton Link 4.1.1 + Asio | fetched (`get_deps.sh`) | GPL-2.0-or-later | tempo / phase / transport sync |
| Signalsmith Stretch 1.4.0 + Linear 0.6.4 | fetched (`get_deps.sh`) | MIT | the audio track's time-stretch |

## 6. Timeline

| Commit | Change |
| --- | --- |
| `e008a5e` | The touch UI mockup (session, grid, pads, FX; three visual styles) |
| `0aa8118` | SLOOP's engine as a C library (`sloop_engine.h`) |
| `94c4e38` | The JUCE app skeleton and its CI (Android APK) |
| `63d22f5` | A sound per track |
| `61ff0cf` | Every parameter on the knobs, live recording, the low-latency audio path (Oboe) |
| `b1c4198` | Projects: save, open, autosave, export, import |
| `3fd2c14` | Long notes in the piano roll |
| `d604048` | QFG drum pads, phone layout |
| `990177a` | Clips up to 64 steps (the rolling-window scheduler), undo, metronome, keys and scales, DAWproject |
| `28d0ef1` | PLAITS |
| `715484f` | 16 parameters per engine; ANALOG rebuilt |
| `1359def` | OBXD (trial), Juno chorus and VB-2 vibrato, sounds by category |
| `f096afb` | Quicker knobs, the full-screen sound browser, recorded notes keep their length |
| `8e4358b` | Eight tracks |
| `0ddce72` | Knob moves recorded, note repeat |
| `a4389f7` | The mixer |
| `0e4fbad` | Recording the output to WAV |
| `46342a1` | MIDI in, clock out, velocity |
| `249a5c2` | Ableton Link |
| `5ae05aa` | The audio track |
| `e4ae825` | These docs |
| — | The app only: the FM-1 firmware removed, the engine's sources in `engine/`, the code split by layer, unit tests, `.sloop` files with their samples |

## 7. Known gaps (see also section 8)

- **Undo** covers the session (clips, scenes, what plays) only. Sound changes, knob moves outside recording, and
  the mix are not in it.
- **Bounce**: no per-track (stem) audio export, and no offline render (recording is live).
- **Audio track routing**: it does not go through SLOOP's reverb and delay; it is mixed after the engine.
- **No iOS build yet.** The code has no Android-only part besides the Link multicast lock and the build files.
- **OBXD's licence** must be cleared before any public release.
- **`engine/core`** still carries SLOOP's FM-1 vocabulary and a few hardware-era leftovers (the USB audio limiter
  in `fx.c`, the MIDI clock-in path in `seq.c`): harmless, to be pruned when that code is touched.

## 8. The app only

The FM-1 is out of scope: the repository became the app alone.

- **Removed**: the FM-1 firmware (its hardware layer, UI, loader, USB and UART MIDI), the web installer and editor,
  the firmware's docs, tests and build scripts, and the tools and assets only they used. That includes Hügelton
  Instruments' all-rights-reserved assets (icons, panel image, the sample pack of `gen_waves.py`): the app never
  used them (its drum kit is the CC0 one). The history keeps all of it.
- **Kept, moved**: the DSP, voices, effects and sequencer SLOOP's preview built (`firmware/src` → `engine/core`,
  29 files), its host adapter (`firmware/host/preview.c` → `engine/host.c`, with the USB and UART MIDI stubbed out),
  the table generators (`tools/gen_*.py`) and the CC0 samples (`assets/samples-cc0`). The generated tables are the
  same, byte for byte.
- **The new layout**: `app/`, `engine/`, `tests/`, `third_party/`, `tools/`, `docs/`, `mockup/`; one CMake project
  at the root (the app and its unit tests); `.github/workflows/app.yml`.
- **The code split by layer** (`app/Source/model`, `audio`, `control`, `ui`): the controller in a file per domain,
  a file per screen, the scheduler's rules and undo as plain code (`audio/Scheduler`, `control/UndoHistory`),
  the clips' edits on `ClipData`.
- **Unit tests** (`tests/`, `SloopTests`): the scheduler, the clips, undo, tempo guesses, projects and their
  migration, bundles, the DAWproject. They found a bug: clearing a lock of a clip holding all 256 lost them all.
- **Exported `.sloop` files carry their samples** (a zip: `project.json` and `samples/`); importing puts the samples
  in the app's folder (a different sound under the same name gets a name of its own).
