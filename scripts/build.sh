#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
base=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $EUID != 0 ]] || { echo 'Build as your ordinary user, not root.' >&2; exit 1; }
for program in git tar patch meson ninja cc c++ pkg-config python3 sha256sum; do
    command -v "$program" >/dev/null || { echo "Missing build tool: $program" >&2; exit 1; }
done
pkg-config --atleast-version=2.68 glib-2.0
pkg-config --atleast-version=3.0 openssl
pkg-config --exists gio-unix-2.0 gobject-2.0 gmodule-2.0 gusb
work="$base/.work"
[[ ! -L "$work" ]] || { echo '.work must not be a symlink.' >&2; exit 1; }
mkdir -p "$work"
revision=037912c17992d2abc81c79c40e85b45ba1d3871e
upstream=https://github.com/lbssousa/libfprint.git
if [[ ! -d "$work/upstream.git" ]]; then git init --bare "$work/upstream.git"; fi
if ! git -C "$work/upstream.git" cat-file -e "$revision^{commit}" 2>/dev/null; then
    git -C "$work/upstream.git" fetch --depth=1 "$upstream" "$revision"
fi
[[ $(git -C "$work/upstream.git" rev-parse "$revision^{commit}") == "$revision" ]]
# Reconstruct from the pinned commit every time; no stale or modified vendor tree.
rm -rf -- "$work/source" "$work/stage" "$work/tested.json"
mkdir -p "$work/source"
git -C "$work/upstream.git" archive "$revision" | tar -x -C "$work/source"
patch --batch --fuzz=0 -d "$work/source" -p1 < "$base/patches/0001-existing-key-integration.patch"
setup=()
if [[ -f "$work/build/meson-private/coredata.dat" ]]; then setup+=(--wipe); fi
meson setup "${setup[@]}" "$work/build" "$work/source" \
    --prefix=/opt/goodix-538d-existing --libdir=lib \
    -Ddrivers=goodixtls53xd -Dintrospection=false -Ddoc=false \
    -Dinstalled-tests=false -Dudev_rules=disabled -Dudev_hwdb=disabled \
    -Dbuildtype=debugoptimized
meson compile -C "$work/build"
"$base/scripts/test.sh"
DESTDIR="$work/stage" meson install -C "$work/build" --no-rebuild
for tool in probe cancel-probe; do
    # pkg-config word splitting is intentional.
    cc -g -Wall -Wextra -Werror "$base/tools/$tool.c" \
        -I "$work/source/libfprint" -I "$work/build/libfprint" \
        -L "$work/build/libfprint" -lfprint-2 \
        $(pkg-config --cflags --libs gio-2.0 gobject-2.0) -o "$work/stage/$tool"
done
python3 "$base/scripts/manage.py" manifest
echo 'Build and offline checks passed. Follow docs/INSTALL.md before hardware use.'
