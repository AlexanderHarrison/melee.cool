#!/bin/bash

set -e

if [ "$1" = 'release' ]; then
    BASE_FLAGS="-O2"
else
    BASE_FLAGS="-ggdb"
fi
WARN_FLAGS="-Wall -Wextra -Wno-char-subscripts -Wno-unused-function -Wint-conversion -Wuninitialized -Wdisabled-optimization -Winit-self -Wlogical-op -Wmissing-include-dirs -Wredundant-decls -Wshadow -Wundef -Wstrict-prototypes -Wpointer-to-int-cast -Wint-to-pointer-cast -Wconversion -Wduplicated-cond -Wduplicated-branches -Wformat=2 -Wshift-overflow=2 -Wint-in-bool-context -Wvector-operation-performance -Wvla -Wdisabled-optimization -Wredundant-decls -Wmissing-parameter-type -Wold-style-declaration -Wlogical-not-parentheses -Waddress -Wmemset-transposed-args -Wmemset-elt-size -Wsizeof-pointer-memaccess -Wwrite-strings -Wtrampolines -Werror=implicit-function-declaration"
F_FLAGS="-funsigned-char -fmax-errors=1"
PATH_FLAGS="-I. -I/usr/include -I/usr/lib -I/usr/local/lib -I/usr/local/include"
LINK_FLAGS="-lssl -lcrypto"

mkdir -p build

if [ ! -f build/mongoose.o ]; then
    /usr/bin/gcc ${F_FLAGS} -DMG_TLS=MG_TLS_OPENSSL -c -O2 vendor/mongoose.c -o build/mongoose.o
fi

if [ "$1" = 'test' ]; then
    /usr/bin/gcc -DTEST ${F_FLAGS} ${WARN_FLAGS} ${PATH_FLAGS} ${BASE_FLAGS} -fsanitize=address -fsanitize=undefined src/tests.c src/server.c build/mongoose.o ${LINK_FLAGS} -o build/server 
else
    /usr/bin/gcc ${F_FLAGS} ${WARN_FLAGS} ${PATH_FLAGS} ${BASE_FLAGS} src/server.c build/mongoose.o ${LINK_FLAGS} -o build/server 
fi

if [ "$1" = 'release' ]; then
    strip build/server
fi
