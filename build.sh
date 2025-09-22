#!/bin/bash

set -e

WARN_FLAGS="-Wall -Wextra -Wno-unused-function -Wuninitialized -Wcast-qual -Wdisabled-optimization -Winit-self -Wlogical-op -Wmissing-include-dirs -Wredundant-decls -Wshadow -Wundef -Wstrict-prototypes -Wpointer-to-int-cast -Wint-to-pointer-cast -Wconversion -Wduplicated-cond -Wduplicated-branches -Wformat=2 -Wshift-overflow=2 -Wint-in-bool-context -Wvector-operation-performance -Wvla -Wdisabled-optimization -Wredundant-decls -Wmissing-parameter-type -Wold-style-declaration -Wlogical-not-parentheses -Waddress -Wmemset-transposed-args -Wmemset-elt-size -Wsizeof-pointer-memaccess -Wwrite-strings -Wtrampolines -Werror=implicit-function-declaration"
if [ "$1" = 'release' ]; then
    BASE_FLAGS="-O2"
else
    BASE_FLAGS="-ggdb"
fi
PATH_FLAGS="-I/usr/include -I/usr/lib -I/usr/local/lib -I/usr/local/include"
LINK_FLAGS="-lssl -lcrypto"

mkdir -p build

if [ ! -f build/mongoose.o ]; then
    /usr/bin/gcc -DMG_TLS=MG_TLS_OPENSSL -c -O2 vendor/mongoose.c -o build/mongoose.o
fi
if [ ! -f build/hescape.o ]; then
    /usr/bin/gcc -c -O2 vendor/hescape.c -o build/hescape.o
fi

/usr/bin/gcc -fmax-errors=1 ${WARN_FLAGS} ${PATH_FLAGS} ${BASE_FLAGS} server.c build/mongoose.o build/hescape.o ${LINK_FLAGS} -o build/server

if [ "$1" = 'release' ]; then
    strip build/server
fi
