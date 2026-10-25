# Obxd (Vadim Filatov) for SLOOP Touch: a trial

The voice of **Obxd**, Vadim Filatov's Oberheim OB-X emulation (2013-2014, github.com/2DaT/Obxd, commit
5f8fb31): its two oscillators, its filter, its envelopes. The app's OBXD engine (`mobile/engine/eng_obxd.c`,
`obxd_glue.cc`) plays one `ObxdVoice` per SLOOP voice; Obxd's plugin, voice allocation, LFO and parameters are
not used.

Licence: the project is distributed under the GPL version 3 (`LICENSE`, the repository's `license.txt`); the
headers of its source files say "GNU General Public License Version 2". **To be cleared before a release** (a
word with the author, or the GPL-3 discoDSP releases of OB-Xd 2.x as the source); this is a trial.

Changes: `Engine/SynthEngine.h` is not Obxd's: it replaces it with what the voice's headers used from JUCE
(`Random`, `jmin`, `jlimit`, `roundToInt`, `zeromem`, `float_Pi`). The other files are as they were.
