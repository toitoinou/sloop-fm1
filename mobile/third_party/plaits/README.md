# Plaits (Mutable Instruments) for SLOOP Touch

The DSP of Mutable Instruments' **Plaits** macro-oscillator by Emilie Gillet, and the parts of **stmlib** it uses,
under the MIT license (`LICENSE`). The app's PLAITS engine (`mobile/engine/eng_plaits.c`, `plaits_glue.cc`) plays
it; the FM-1 firmware does not build it (`FELUCCA_PLAITS`, `firmware/src/core.h`).

Taken from github.com/pichenettes/eurorack (`plaits/dsp`, `plaits/resources.*`, `plaits/user_data.h`, commit
f4cfa76) and github.com/pichenettes/stmlib (`stmlib.h`, `dsp/`, `utils/`, commit d18def8).

Changes, all marked `SLOOP Touch`:

- `plaits/dsp/dsp.h`: `kSampleRate` and `kCorrectedSampleRate` are 44100 (SLOOP's engine rate), so the pitch,
  the envelopes and the filters are right at that rate;
- `plaits/dsp/fx/diffuser.h`: its LFO rate from `kSampleRate` (it divided by 48000);
- `plaits/user_data.h`: no flash: no user data (the factory wavetables and FM banks), no flash writes;
- `stmlib/stmlib.h`: `TEST` defined, stmlib's portable code paths (no Cortex-M assembly, no RAM sections);
- `stmlib/utils/crc32.h` and `murmurhash3.h` left out (not used, not MIT).
