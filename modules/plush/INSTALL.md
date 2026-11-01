# Plush on a Canon Kiss X4 (550D)

Run `modules/plush/build_550d_macos.sh` (see the top of the script for what it
needs). It makes `modules/plush/card/`, which holds everything that goes on the
SD card: Magic Lantern for the 550D, the `plush` module, and the plush images.

## One-time install

1. **Check the Canon firmware.** Menu → wrench tab → *Firmware Ver.* must say **1.0.9**.
   If it's older, install Canon's 1.0.9 update first.
2. **Charge the battery fully.**
3. **Format the card in the camera** (32 GB or smaller works best for ML's boot flag).
4. **Copy the contents of `card/`** to the root of the card, so `autoexec.bin`,
   `ML-SETUP.FIR` and `ML/` are at the top level.
5. Put the card back, set the mode dial to **M**, then Menu → wrench tab →
   *Firmware Ver. 1.0.9* → update. This runs ML's installer (not a real firmware
   update). Follow the on-screen prompts, then turn the camera off and on.
6. Press the **Trash** button to open ML's menu.

## Turn on the plush

1. ML menu → **Modules** tab → select **plush** → SET to load it on boot.
2. Restart the camera.
3. ML menu → **Overlay** → **Plush** is on by default. Its submenu has *Shuffle now*.

The plush jumps to a new random spot and size (0.3"–1" tall) whenever the screen
changes: LiveView ↔ playback ↔ Canon menu ↔ ML menu, and on each INFO press.

Optional: ML menu → Overlay → **Crop marks** → `PLUSH.BMP` shows a fixed,
centered plush in LiveView only, with no module needed.

## Expected quirks

- Canon redraws its own screens whenever it likes, so the plush may flicker for a
  moment before it's repainted (it checks 10× a second).
- Switching ML menu tabs doesn't count as a screen change.
- The Lua scripting module is left out of this build (it doesn't compile with this
  toolchain), so `ML/scripts` won't run.

## Uninstall

Run the firmware update again with this card in, and choose to remove the boot
flag. Or just use a card without ML on it: the camera then boots normally.

## Changing things

- Different image: replace `modules/plush/plush.webp` (ideally with a transparent background).
  `make_cropmark.py` converts it; the build script runs it for you.
- Module code: `modules/plush/plush.c`.
- Rebuild everything and refresh `card/`: `modules/plush/build_550d_macos.sh`.
