# Licensing

SLOOP Touch is free software. Its code is licensed under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). Every source file of ours carries an `SPDX-License-Identifier` header.

The engine (`engine/core/`) comes from SLOOP (isod89), built on Felucca:
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, and SLOOP's contributors, GPL-3.0-only.
SLOOP's assets that are not under the GPL (Hügelton Instruments' icons, panel image and sample pack) are **not**
in this repository and are not used by the app.

## Third-party code and data

| What | Licence | Where |
| --- | --- | --- |
| JUCE 8.0.4 (fetched, not in the repository) | AGPLv3 or commercial (AGPLv3 combines with GPL-3) | `app/JUCE` |
| Mutable Instruments Plaits and stmlib (Emilie Gillet) | MIT | `third_party/plaits` (its changes in its README) |
| Obxd's voice (Vadim Filatov, 2DaT) | GPL (2 or 3: **to be cleared before a release**) | `third_party/obxd` |
| Ableton Link and Asio (fetched) | GPL-2.0-or-later; Boost | `third_party/link` (`tools/get_deps.sh`) |
| Signalsmith Stretch and Linear (fetched) | MIT | `third_party/signalsmith-*` (`tools/get_deps.sh`) |
| msfa (Google Inc., Pascal Gauthier, from Dexed): the FM6 engine's synthesis | Apache-2.0 | `engine/core/fm6_core.c`, `LICENSES/Apache-2.0-msfa.txt` |
| DaisySP (Electrosmith, Emilie Gillet) and Rings' sympathetic strings: the PHYS engine's models | MIT | `engine/core/phys_dsp.c`, `engine/core/phys_symp.c` (the notice is in each file) |
| CrispyZebra (Leo Kuroshita): the PHASE engine's waveforms | GPL-3.0 | `engine/core/eng_phase.c` |
| klattsch (Tony Gies): a design reference for the VOICE engine, no code copied | MIT | credit only |
| Instrument and drum samples (Versilian Studios VSCO-2 CE, VCSL; Sonic Pi) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "Ableton" and "Link" are trademarks of
Ableton AG. "M-VAVE" and "FM-1" are trademarks of their owners. This app is not affiliated with any of them.
