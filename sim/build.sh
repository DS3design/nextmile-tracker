#!/bin/bash
# Builds the desktop renderer (needs libsdl2-dev). LovyanGFX objects are cached in build/obj.
set -e
cd "$(dirname "$0")"
L=${LGFX_SRC:-../firmware/.pio/libdeps/cyd_ili9341/LovyanGFX/src}
F="-std=c++17 -O1 -DLGFX_SDL -I$L $(sdl2-config --cflags)"
mkdir -p build/obj out
for src in $L/lgfx/v1/*.cpp $L/lgfx/v1/misc/*.cpp $L/lgfx/v1/panel/Panel_Device.cpp $L/lgfx/v1/panel/Panel_FrameBufferBase.cpp $L/lgfx/v1/platforms/sdl/*.cpp $L/lgfx/utility/*.c $L/lgfx/Fonts/efont/*.c $L/lgfx/Fonts/IPA/*.c; do
  o=build/obj/$(echo $src | md5sum | cut -c1-10).o
  [ -f $o ] || { case $src in *.c) gcc -O1 -I$L -c $src -o $o & ;; *) g++ $F -c $src -o $o & ;; esac; }
done
wait
g++ $F sim_main.cpp ../firmware/src/ui.cpp build/obj/*.o -o build/sim $(sdl2-config --libs) -lpthread
./build/sim
for f in out/*.ppm; do python3 -c "from PIL import Image;import sys;Image.open(sys.argv[1]).save(sys.argv[1][:-4]+'.png')" $f; rm $f; done
ls out
