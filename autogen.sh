#!/bin/sh
aclocal
autoreconf -i -f
./configure $@
