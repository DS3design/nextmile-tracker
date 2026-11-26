# Next Mile Tracker Flasher

Standalone Windows app: plug a board in, pick its USB port (or let it pick the
CH340 one), click **Detect board**, then **Flash**. It flashes the probe first,
reads which display chip the board has over serial, picks the matching firmware,
and installs it. If the screen comes up upside down, click **Screen is upside
down** to flash the rotated variant.

The exe is built with PyInstaller:

```
python -m PyInstaller --noconfirm --onefile --windowed --collect-all esptool --name NextMileFlasher flasher.py
```

The `firmware/` folder must sit next to the exe.

## Adding a new firmware build (same board)

1. Drop the new `.bin` into `firmware/cyd/`.
2. Add it to the `firmwares` list in `firmware/manifest.json` with a unique `id`,
   a display `name`, and the `file` name.

## Adding a new board (e.g. CrowPanel 7")

1. Make a new subfolder like `firmware/crowpanel7/` with its bootloader,
   partitions, boot_app0, filesystem image, and firmware `.bin` files.
2. Add a new entry to `boards` in the manifest:
   - `base`: the `[offset, file]` pairs always written (bootloader, partitions, boot_app0, filesystem)
   - `app_offset`: where the firmware goes (usually `0x10000`)
   - `probe`: omit it if the board has no detection probe — the firmware is then
     picked manually from the dropdown
   - `firmwares`: the selectable builds
   - `detect_map`: optional mapping from probe `RESULT=` values to firmware ids

If a firmware `id` has a rotated variant, name it `<id>_flip` and the rotate
button finds it automatically.

## Probe firmware

`probe/` is a tiny PlatformIO project: it boots, tries the ILI9341 and ST7789
drivers, prints `RESULT=ILI9341` / `RESULT=ST7789` over serial, and halts.
Detection works because only the correct driver can read back a drawn pixel.
Rebuild with `pio run` in `probe/` and copy `.pio/build/cyd_probe/firmware.bin`
to `firmware/cyd/probe.bin`.
