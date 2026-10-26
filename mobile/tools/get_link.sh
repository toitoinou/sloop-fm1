#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Ableton Link (GPL-2.0-or-later, header-only) and the Asio it uses, into mobile/third_party/link (not in the
# repository, as JUCE). The app's builds (CMake, the Projucer project) read its headers there.
#   mobile/tools/get_link.sh
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="$ROOT/mobile/third_party/link"
TAG=Link-4.1.1
[ -f "$DIR/include/ableton/Link.hpp" ] && [ -f "$DIR/modules/asio-standalone/asio/include/asio.hpp" ] && exit 0
rm -rf "$DIR"
git clone --depth 1 --branch "$TAG" https://github.com/Ableton/link.git "$DIR"
git -C "$DIR" submodule update --init --depth 1 modules/asio-standalone
