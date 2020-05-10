#!/bin/bash
autoreconf -fiv

rm -rf build/
mkdir -p build && cd build/

../configure --prefix=/usr --sysconfdir=/etc --disable-sanitizers

make install

install -Dm644 LICENSE "/usr/share/licenses/i3lock-color/LICENSE"
