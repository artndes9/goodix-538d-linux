#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
base=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
src="$base/.work/source"
build="$base/.work/build"
common=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
cc "${common[@]}" -Wall -Wextra -Werror -I "$src/libfprint/drivers/goodixtls" \
    "$base/tests/tls-tests.c" "$src/libfprint/drivers/goodixtls/goodixtls.c" \
    $(pkg-config --cflags --libs gio-2.0 openssl) -o "$build/tls-tests"
cc "${common[@]}" -I "$src" "$base/tests/existing-key-tests.c" \
    "$src/libfprint/drivers/goodixtls/goodix_psk.c" \
    $(pkg-config --cflags --libs gio-2.0 openssl) -o "$build/existing-key-tests"
cc "${common[@]}" -I "$build" -I "$build/libfprint" -I "$src/libfprint" \
    -I "$src/libfprint/drivers/goodixtls" "$base/tests/transport-tests.c" \
    "$src/libfprint/drivers/goodixtls/goodixtls.c" "$src/libfprint/drivers/goodixtls/goodix_proto.c" \
    -L "$build/libfprint" -Wl,-rpath,"$build/libfprint" -lfprint-2 \
    $(pkg-config --cflags --libs gio-2.0 gusb openssl) -o "$build/transport-tests"
for test in existing-key-tests tls-tests transport-tests; do
    G_DEBUG=fatal-warnings "$build/$test"
done
meson test -C "$build" --print-errorlogs --suite unit-tests --suite data
python3 -m unittest discover -s "$base/tests" -p 'test_*.py' -v
