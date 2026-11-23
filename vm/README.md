# VM Fantasy Console

The VM Fantasy Console is a single-binary SDL2 desktop environment for writing
assembly and editing 128x128, 4bpp games with the 16-color Sweetie-16 palette.
Run `make` to build and `make run` (or `./vm`) to open the console launcher.
The existing Arkanoid program remains available as **Run cartridge** and its
assembly source is loaded into the code editor on startup.

## Console controls

- Launcher: Up/Down selects a tool, Enter opens it, Escape quits.
- Code: Up/Down selects a line, Enter edits it, Left/Right moves the caret,
  Backspace/Delete edits, Enter commits, Escape cancels, F5 assembles and runs,
  Ctrl+S saves, Ctrl+O loads.
- Sprite: arrows move the pixel, Z paints, X erases, brackets change color,
  comma/period change sprite.
- Tile map: arrows move the cell, Z places the selected tile, brackets change
  the tile ID.
- SFX: Left/Right selects a sound, Tab selects a parameter, Up/Down adjusts it,
  Space previews it.
- Music: arrows select a step and channel, Z/X raise/lower its note, Space plays
  the pattern.
- Editors: Escape returns to the launcher. Save/load prompts accept a path and
  default to `cartridge.cart`.

The assembly editor stores source in the cartridge. F5 invokes the existing VM
assembler; the VM's instruction set, framebuffer, input registers, font address,
and 24 KB memory map are unchanged.

## Cartridge format

Cartridges are little-endian binary files with a 14-byte header: `VMFC` magic,
16-bit format version (currently 1), 32-bit payload size, and CRC-32 of the
payload. The payload contains a 32-byte title, a 32-bit source length and UTF-8
assembly source, 256 packed 8x8 sprites (32 bytes each, two 4-bit pixels per
byte), a 16x16 tile map, 16 four-byte sound effects, and 16 steps of four music
channels. Load validates the version, lengths, checksum, and asset value ranges
before replacing the active cartridge.

Run `make test` from this directory for cartridge serialization and editor-data
operation tests.
