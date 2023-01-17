#!/bin/bash
cd $(dirname $(readlink -f $0))
cd ../third_party/dash/third_party/pmdk/src/PMDK
sed -Ei 's/MAP_FIXED_NOREPLACE/MAP_FIXED/g' src/common/set.c 
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/home/linuxbrew/.linuxbrew/lib/pkgconfig:/usr/share/pkgconfig
make EXTRA_CFLAGS=-Wno-error -j
cd -