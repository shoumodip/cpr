# C Package Resolver

## Quick Start
Calls `pkg-config` as an external process

```console
$ ./build.sh
$ ./cpr flags xft x11
-I/usr/include/freetype2 -I/usr/include/libpng16
$ ./cpr libs xft x11
-lXft -lX11
```

## Packages in `CPRPATH`
```console
$ export CPRPATH=/opt
$ ./cpr flags xft raylib
-I/usr/include/freetype2 -I/usr/include/libpng16 -I/opt/raylib/include
```
