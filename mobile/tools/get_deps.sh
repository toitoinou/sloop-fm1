#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The libraries the app fetches (not in the repository, as JUCE), into mobile/third_party:
#   link/                 Ableton Link 4.1.1 (GPL-2.0-or-later, header-only) and the Asio it uses
#   signalsmith-stretch/  Signalsmith Stretch 1.4.0 (MIT): the audio clips' time-stretch
#   signalsmith-linear/   Signalsmith Linear 0.6.4 (MIT): its FFT
# The app's builds (CMake, the Projucer project) read their headers there.
#   mobile/tools/get_deps.sh
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="$ROOT/mobile/third_party"
get() {   # name url tag [submodule]
    [ -f "$DIR/$1/.fetched-$3" ] && return 0
    rm -rf "$DIR/$1"
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$3" "$2" "$DIR/$1"
    [ $# -lt 4 ] || git -C "$DIR/$1" submodule -q update --init --depth 1 "$4"
    touch "$DIR/$1/.fetched-$3"
}
get link https://github.com/Ableton/link.git Link-4.1.1 modules/asio-standalone
get signalsmith-stretch https://github.com/Signalsmith-Audio/signalsmith-stretch.git 1.4.0
get signalsmith-linear https://github.com/Signalsmith-Audio/linear.git 0.6.4
