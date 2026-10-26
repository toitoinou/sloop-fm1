# SLOOP Touch: software architecture

A guide for a developer opening the code for the first time:
- the layers;
- the threads and how they talk;
- where each thing lives;
- how the main features work inside;
- how to build, test and extend.

For the project's history (what came from SLOOP, what changed), read [HISTORY.md](HISTORY.md).

## 1. The layers

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ UI  (JUCE, message thread)                       mobile/app/Source                       │
│   MainComponent: tabs, transport, sheets         Views: TrackRail, SessionView/Grid,     │
│   AudioEditor, ProjectSheet, SettingsSheet,      StepGrid, PadsView, KnobStrip,          │
│   SoundSheet, StepSheet                          MixerView                               │
├──────────────────────────────────────────────────────────────────────────────────────────┤
│ Controller (message thread): the app's state and every edit                             │
│   session (clips, scenes), selection, playlists, undo, recording, mixer, MIDI, samples, │
│   Link state. Turns gestures into AudioEngine commands; reads back atomics.             │
├────────────────────────── lock-free command FIFO ▼ ─── atomics ▲ ───────────────────────┤
│ AudioEngine (audio thread; juce::AudioIODeviceCallback)                                 │
│   applies commands between engine blocks · the rolling-window scheduler (writes the     │
│   steps just ahead of the playhead) · note repeat · the audio track (ClipPlayer) ·      │
│   Link (LinkSync) · MIDI in · resampling 44.1 kHz → device rate · WAV recorder          │
├──────────────────────────────────────────────────────────────────────────────────────────┤
│ sloop_engine (C API, se_*)                         mobile/engine                         │
│   + PLAITS (Mutable Instruments Plaits), OBXD (Obxd voice): the app's own engines        │
├──────────────────────────────────────────────────────────────────────────────────────────┤
│ firmware/host/preview.c → firmware/src/*.c (one translation unit: SLOOP's DSP, voices,   │
│ effects, sequencer and clock; the hardware stubbed out)                                  │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

**The main idea: the session lives in the app, and SLOOP only plays.**

- SLOOP's sequencer loops each track over its 64 steps. The app does not store the music in the engine.
- Just ahead of the playhead, the audio thread writes the few steps due there. They come from whichever clip should
  play at that point, with that pass's dice and conditions resolved. This is the rolling window, section 4.1.
- So the app gets clips of any length, chains, launch at the bar, probabilities and fills. It keeps SLOOP's DSP and
  its sample-accurate timing.

## 2. Threads and how they talk

| Thread | Who | Rules |
| --- | --- | --- |
| Message (UI) | JUCE components, `Controller`, timers (30 Hz) | Owns all app state. Never touches the engine directly. |
| Audio | `AudioEngine::audioDeviceIOCallbackWithContext` | No locks, no allocation, no file or system calls. The one exception is `writerLock`, taken only while a recording starts or stops. It owns the engine (`se_*` is not thread-safe). |
| MIDI input | JUCE's MIDI callbacks (`MidiHub`) | Notes go straight to `AudioEngine::midiIn` (a `MidiMessageCollector`). CC and transport messages are posted to the message thread. |
| MIDI clock out | `MidiHub` timer (`HighResolutionTimer`) | Reads `ctl.bpm` and `audio.running`. |
| Link | Ableton Link's own threads (Asio) | `LinkSync` hides them. The audio thread uses `captureAudioSessionState` and `commitAudioSessionState` only. |
| WAV writer | `juce::TimeSliceThread` + `ThreadedWriter` | The audio thread pushes into its FIFO. |

**UI → audio**

- `AudioEngine::post(Command)` writes into a `juce::AbstractFifo` of 64 commands.
- The audio thread applies every waiting command at the start of each callback (`apply`).
- A `Command` carries everything by value, including copies of clips. The audio thread never reads UI memory.
  - One exception: `ClipPlayer::Clip::sample` is a raw pointer into a sample owned by `SampleBank`.
  - The bank frees a sample only 2 s after the last project stops using it (`SampleBank::keepOnly`).

**Audio → UI**

- Atomics in `AudioEngine`: `playingSlot`, `position`, `barStep`, `running`, `countIn`, `paramValue` (+
  `paramSerial`), the meters (`peak`, `audioPeak`, `masterPeak`), the audio track's `audioSlot` / `audioProgress`,
  and Link's `linkSerial` / `linkTempo` / `linkPlaying`.
- `Controller::poll()` (30 Hz, from `MainComponent`'s timer) reads them back. It clears "waiting" clips once they
  play, picks up a new sound's parameters, follows Link, and finishes recorded notes.

## 3. Where things live

### `mobile/app/Source/`

| File | Responsibility |
| --- | --- |
| `Main.cpp` | The JUCE application; command-line modes `--selftest`, `--snapshot`, `--dawproject`; autosave on suspend and quit. |
| `MainComponent.*` | The one window: layout (phone / tablet, landscape / portrait), tabs, transport buttons, opening sheets, audio device setup (Oboe, buffer size), sharing a recording, importing a sound. |
| `Model.h` | Plain data: `StepData`, `ClipData` (steps, notes, lengths, locks), `Clip`, `AudioClip`, `Session` (columns × scenes + the audio column), keys and scales, track names and colours, constants (`kTracks` 8, `kMaxSteps` 64, `kRows` 24, `kAudio`...). |
| `Controller.h` | The app's state and every edit. Header-only, about 1000 lines. Sections: session ops, scenes, clipboard, undo, steps, recording (hits, releases, knob motion, note repeat), knob pages, mixer, MIDI, the audio track, Link, projects. |
| `AudioEngine.*` | The audio thread, described in section 4. |
| `ClipPlayer.*` | The audio track's player: a loop placed on the song's beats; stretched (Signalsmith) or varispeed (Hermite interpolation). |
| `Samples.*` | `SampleBank`: imports (decode, resample to 44.1 kHz, write a WAV into `samples/`), loads into memory, keeps them alive. |
| `LinkSync.*` | Ableton Link behind a small interface. The only file that sees Link's and Asio's headers. On Android it also holds the Wi-Fi multicast lock (JNI). |
| `Midi.*` | `MidiHub`: opens every MIDI input, Bluetooth pairing, MIDI clock out. |
| `Project.*` | `.sloop` JSON: write / read, migrating older formats; `Prefs`; the app's folders (`store::`). |
| `DawProject.*` | The DAWproject export (`project.xml`, `metadata.xml`, the samples, in a zip). |
| `Views.*` | Every screen but the sheets listed below: TrackRail, SessionGrid / SessionView, StepGrid, PadsView, KnobStrip, MixerView, SoundSheet, SettingsSheet, StepSheet, small widgets. About 1900 lines. |
| `AudioEditor.*` | The audio clip editor. |
| `ProjectSheet.*` | Projects: list, save as, open, export, import, DAWproject. |
| `Theme.h` | Colours, fonts, the flat look-and-feel. |
| `SelfTest.cpp` | `--selftest`: the end-to-end checks of the audio side. |

### `mobile/engine/`

| File | Responsibility |
| --- | --- |
| `sloop_engine.h` | The C API the app uses (`se_*`). Read its comments: they are the contract. |
| `sloop_engine.c` | Sets the build (`NPART 7`, `VOICE_BUDGET 32`, `FELUCCA_PLAITS`, `FELUCCA_OBXD`) and includes `firmware/host/preview.c`. Implements `se_*` on SLOOP's internals. Static asserts tie its ids to `core.h`. |
| `eng_plaits.c`, `plaits_glue.cc/.h` | The PLAITS engine (an `engine_t`) and its C++ bridge to Plaits' voices. |
| `eng_obxd.c`, `obxd_glue.cc/.h` | The OBXD engine and its bridge to `ObxdVoice`. |
| `gen/` | Generated tables (not in git): `mobile/tools/gen_engine_tables.sh`. |

### Elsewhere

| Path | What |
| --- | --- |
| `firmware/src/` | SLOOP's engine: see HISTORY.md for what the app needs from it. |
| `mobile/third_party/plaits`, `obxd` | Vendored, with a README listing every change. |
| `mobile/third_party/link`, `signalsmith-*` | Fetched by `mobile/tools/get_deps.sh` (git-ignored). |
| `mobile/mockup/` | The HTML mockup the UI follows. |
| `.github/workflows/mobile.yml` | CI: APK, desktop build, self-test, screenshots. |

## 4. How the main features work

### 4.1 The rolling-window scheduler (`AudioEngine.cpp`)

Each app track `t` maps to an engine track: `engineTrack(t)`. DR is SLOOP's drum track, the last one; T1..T7 are
parts 0..6. Each track has a `TrackState`:

- the playlist (1..`kChainMax` clips, copied in);
- the clip playing (`idx`), where its pass started (`start`, in steps since PLAY) and how many passes it has played;
- an optional `next` playlist waiting for `pendingAt` (the next bar);
- `cur` (the step playing) and `written` (how far ahead the engine has been given steps).

On every engine block (32 frames), for each track that moved to a new engine step:

1. The steps behind let go of their parameter locks (`se_set_step_locks` with none).
2. `commit(t, step)`: `locate` finds the clip due at that step, across chain passes and a pending playlist. Then the
   atomics are published: playing row, position in the clip, LED.
3. `writeStep(t, y)` for the steps entering the window (`kAhead` = 4 steps ahead).
   - It resolves the step's probability and condition for that pass: a seeded hash, so a pass is stable.
   - It writes the notes (long notes held with `se_synth_step_cont`) or the drum hits, with nudge and ratchets.
   - It writes the step's locks.

An edit to a playing clip is sent as `When::update` and `rewrite(t)` writes the window again, so edits are heard
at once.

### 4.2 Parameters, knobs and locks

- **Parameters are SLOOP's `P_*` ids** (`core.h`).
  - Below `P_E0` (67): the track parameters (level, envelope, filter, sends, pan, MOD...).
  - From `P_E0` to `P_E0 + 15`: the engine's own parameters.
- **Describing them**: `se_describe` / `se_value_name` / `se_engine_page` give each one's range, label and value
  names, per engine.
- **Pages**: `Controller::pages()` lists the pages for the selected track (`Page::mix`, `macro`, `edit1..4`, `env`,
  `mod`, `fx`). `knobDefs()` gives the 4 knobs of a page; the drum track's Mix page maps to SLOOP globals (`kGlobal`
  ids).
- **Where a knob turn goes**:
  - steps held: locks on those steps of the clip (`ClipData::setLock`, up to 256 per clip);
  - REC on and playing: a lock on the step playing (motion recording);
  - otherwise: the track's value (`audio.setParam`).
- **After a sound change**: a new preset changes many values. The audio thread publishes them (`publishParams`)
  and the UI reads them back.

### 4.3 Recording (`Controller.h`: `recordHit`, `recordRelease`, `recordMotion`, `recordRepeats`)

- **Where a hit goes**: the step it falls on, from `position` and `stepPhase`. It is quantized to the nearest step,
  or kept off the grid as a nudge (`prefs.quantizeRec`).
- **A hit on a step not yet played**: it is written once that step has passed (`pendingRecord`), so it is not
  heard twice.
- **Held notes**: they get their length on release.
- **An empty track**: a hit creates a clip that starts on the bar playing (`barStep`).
- **Undo**: a recording pass is one undo step (`remember("rec")`).

### 4.4 The audio track (`ClipPlayer`, `AudioEngine::renderAudioTrack`)

- **`songBeat`**: the song's beat, counted exactly as the engine's clock moves (`frames × tempo / 60 / 44100` per
  block, or what Link moved it).
- **Where the clip is in its loop**: `(songBeat − origin) mod beats`. It is recomputed every block, so the clip
  cannot drift and follows tempo and Link changes.
- **Varispeed**: each output frame reads the loop at its beat (Hermite interpolation).
- **Stretch**: Signalsmith's input runs ahead of its output.
  - The player feeds it up to `beat(end of block) + inputLatency + rate × outputLatency`.
  - Its output at a beat is then the loop's audio for that beat.
  - At a launch, `outputSeek` primes it so that the first output frame is the loop's start.
  - A jump (lost sync) re-primes it.
- **Launching**: like MIDI clips (`setAudioClip`): at the next bar while playing, at once when stopped. Scenes
  include the audio column.
- **Solo**: the audio track alone mutes the engine's output (`engineSilent`).
- **The `AudioClip` model** (`Model.h`):
  - Fields: `file`, `start`, `end`, `bpm`, `stretch`, `gain`.
  - `beats()` = round(seconds × bpm / 60).
  - The loop plays exactly `beats()` beats of the song.

### 4.5 Ableton Link (`LinkSync`, `AudioEngine::followLink`)

- **Timing**: every callback is stamped with Link's clock. `HostTimeFilter` smooths the device's sample count, and
  the output latency is added. Each engine block gets its own time.
- **Before each engine block**:
  1. What this app asked is committed first: a tempo (`linkTempoReq`) or Play / Stop (`linkPlayReq`).
  2. The session's state is read (`capture`).
  3. A tempo or transport change bumps `linkSerial`; the UI follows it in `poll()`.
  4. Play waits for the session's next bar (quantum 4).
  5. The engine is moved by `se_set_advance(beats)`, to the session's beat at the block's end. It catches up at no
     more than 2× the tempo; when ahead, it waits.
- **The FM-1's tempo**: `G_BPM` (an integer) still follows the session, rounded. It times the delays and the arps.

### 4.6 MIDI (`Midi.cpp`, `AudioEngine` callback)

- **Notes in**: they bypass the UI. The audio thread drains `midiIn` each block and plays them on `midiTrack` (the
  track selected). A note-off goes to the track its note started on (`midiNoteTrack`).
- **Recording, CC and transport**: posted to the message thread (`Controller::midiNote` / `midiCC`).
- **Clock out**: 24 PPQN from a high-resolution timer, plus Start and Stop.

### 4.7 Projects and files (`Project.cpp`)

- **`Project`**: a snapshot of what is saved.
  - The session, tempo, swing, key, volume and globals.
  - Each track's sound and all its parameters (`hasParams`).
  - The audio track's clips (sample file names) and its mix.
- **`.sloop`**: JSON, `"format": 3`.
  - Older files are migrated in `fromJson` (`paramId()`: ids ≥ 63 shift by 4 before format 3).
  - 4-track files load with the other tracks empty.
- **Folders**: `store::folder()` is the app's data folder (`userApplicationDataDirectory/SLOOP Touch`):
  - `projects/*.sloop`;
  - `autosave.sloop` (every 30 s if changed, and on suspend);
  - `prefs.json`;
  - `recordings/`;
  - `samples/`.

### 4.8 Undo (`Controller::remember` / `travel`)

- **What a step back holds**: the whole `Session` plus `editSlot`, the playlists and the audio row. The session is
  plain data, so a copy is cheap enough at this size; 40 steps are kept.
- **Merging**: edits of the same kind within 400 ms merge into one step. A recording pass is always one step.
- **Restoring**: playlists are kept where the clips still exist. The engine gets updated clips (`When::update`) or
  a new playlist.

### 4.9 DAWproject (`DawProject.cpp`)

- **Tracks**: one MIDI track per app track. The audio track is added only when it has clips.
- **The arrangement**: the scenes laid end to end, each as long as its longest clip.
- **The launcher**: the scenes as clip-launcher rows.
- **Notes**: written as the app plays them with no fill held.
- **Audio clips**: `<Warps>` map the clip's beats to its start and end in seconds. The samples go into the zip
  under `audio/`.
- **Validation**: the output validates against the format's `Project.xsd`. Keep it valid when you extend it, for
  example with `xmllint --schema`.

## 5. The engine side in more detail

- **Unity build.** `sloop_engine.c` includes `preview.c`, which includes the firmware's `.c` files. Everything is
  `static`, in one translation unit. The engine is compiled as C with clang, using `-fwrapv -fno-strict-aliasing`
  and the firmware's own warnings silenced.
- **Generated tables.** Wavetables, samples, kits and FM6 patches come from `tools/gen_*.py`. CMake generates them
  in the build folder. For Android, `mobile/tools/gen_engine_tables.sh` writes them into `mobile/engine/gen`.
- **Engines.** Each is an `engine_t` (see `engines.c` and any `eng_*.c`):
  - a name;
  - render and note functions;
  - its EDIT parameters' descriptions;
  - its 4 page titles and 4 "macro" parameters;
  - its presets (`preset_t`, extended with `E2(...)` for parameters 8..15).
  - The order in `ENGINES[]` is the engine index the app saves in projects. **Only append.**
- **Adding an engine to the app:**
  1. Write `eng_x.c`, plus a C++ glue file if its DSP is C++.
  2. Guard it with a `FELUCCA_X` flag in `core.h` and `engines.c`, and set the flag in `sloop_engine.c`.
  3. Allocate its voices in `se_init` (never on the audio thread).
  4. Add the sources to `CMakeLists.txt` and `SloopTouch.jucer`.
  5. Add a line to `engineAbout()` (Views.cpp) and a self-test that its presets sound.
- **Ids.** The `SE_*` constants in `sloop_engine.h` mirror `core.h`. The static asserts fail the build if they
  drift.

## 6. Building, testing, CI

- **Desktop** (Linux, macOS, Windows):
  - `cmake -S mobile/app -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`, then
    `cmake --build build`.
  - Without `-DJUCE_DIR`, CMake fetches JUCE 8.0.4.
  - It also runs `get_deps.sh` (Link, Signalsmith) when they are missing.
- **Android**:
  1. Put a JUCE 8.0.4 checkout in `mobile/app/JUCE`.
  2. Run `mobile/tools/gen_engine_tables.sh` and `mobile/tools/get_deps.sh`.
  3. Run `Projucer --resave mobile/app/SloopTouch.jucer`.
  4. Run `./gradlew assembleRelease` in `mobile/app/Builds/Android`.
  - The CI does exactly this.
  - Header paths in the `.jucer` are relative to `Builds/Android`.
- **`--selftest`** (`SelfTest.cpp`): an end-to-end check of the audio side. There is no device: the test calls the
  audio callback itself, at 48 kHz in 256-frame blocks. It covers:
  - launches at the bar, chains, sounds, recording, undo, projects, DAWproject;
  - every PLAITS and OBXD preset sounding;
  - MIDI, solo, the WAV recorder, Link (in real time);
  - the audio track's timing in both modes.
  - **To add a check:** drive the `Controller` like the UI does, `run(seconds)`, then
    `check(condition, "what it proves")`.
- **`--snapshot out.png WxH tab`**: renders a screen to look at layouts without a device. The tab is one of
  `session`, `grid`, `pads`, `mix`, `project`, `settings`, `soundN`, `trackN`, `audio`. The CI uploads several.
- **CI** (`.github/workflows/mobile.yml`): two jobs, the Android APK (`sloop-touch-apk` artefact), and the desktop
  build + self-test + screenshots.

## 7. Conventions

- **Licence header**: `// SPDX-License-Identifier: GPL-3.0-only` on every file.
- **Comments**: short, factual, and in the code's terms. They say what a thing is and why, not how. They follow the
  firmware's style (e.g. "the steps just ahead of the playhead").
- **Text**: the UI's strings are French, in place (no i18n yet). Use `juce::String::fromUTF8` for accents. Code,
  comments and docs are English; the user-facing README is French.
- **Audio thread**: no allocation. Buffers are sized up front (`std::array`, `reserve`), and copies come by value
  in a `Command`.
- **Warnings**: JUCE's recommended warning flags are on for the app (`-Wshadow`, conversions…). Keep the build
  warning-free. Third-party headers come in with `-isystem` (CMake) or `-w`.
- **Third-party changes**: vendored code changes are listed in its `README.md` and marked in the source.

## 8. Recipes

- **A new command to the audio thread:**
  1. Add a `Type`.
  2. Add a message-thread method that fills a `Command` and `post`s it.
  3. Handle it in `AudioEngine::apply` (no allocation there).
  4. If the UI needs a result, publish an atomic and read it in `Controller::poll`.
- **A new field saved in projects:**
  1. Add it to `Project`.
  2. Add it to `toJson` / `fromJson`, with a default for older files.
  3. Add it to `Controller::snapshot` and `load`.
  4. Add a round-trip check to the self-test.
  - Bump `kFormat` only if older apps would misread the file.
- **A new knob page:** add a `Page` and its entry in `Controller::pages()` and `knobDefs()`; the page tab appears in
  `KnobStrip`.
- **A new screen:** a `juce::Component` in `Views.*`, or in its own file if it is large (as `AudioEditor`). Add a
  tab in `MainComponent`, and a `--snapshot` name in `showTabNamed`.

## 9. Things to know before you change something

- **`Command` is large** (it carries up to 8 `ClipData` by value: about 120 KB). The FIFO holds 64.
  Loading a project posts a few dozen commands at once. If you add many more per project, grow the FIFO or slim
  the command.
- **Engine track numbers differ from the app's.** DR is the engine's last track (`engineTrack`). Always convert.
- **The engine renders 32-frame blocks at 44.1 kHz.** The app resamples them to the device's rate (Lagrange). Any
  timing in the audio thread is counted in engine blocks.
- **`se_*` must be called from the audio thread.** The exceptions are the constant tables (names, descriptions).
- **Samples are kept whole in memory**, as stereo float at 44.1 kHz (10 minutes at most, about 200 MB).
- **Sound and mix changes are outside undo.**
- **The firmware sources are shared with the FM-1 build.** A change in `firmware/src` changes both. Guard
  app-only features with a flag, as `FELUCCA_PLAITS` does.
