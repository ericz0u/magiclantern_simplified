#!/bin/bash
# Build Magic Lantern for the 550D (Kiss X4) with the plush module on macOS,
# and assemble card/ with everything that goes on the SD card.
#
#   modules/plush/build_550d_macos.sh
#
# Needs:
#   ARM's official GCC (has the C library headers Homebrew's arm-none-eabi-gcc lacks):
#     https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#     unpacked to ~/opt/arm-gnu-toolchain-15.3.rel2-darwin-arm64-arm-none-eabi (or set ARM_TC)
#   Python with docutils and Pillow, e.g.:
#     python3 -m venv ~/opt/ml-venv && ~/opt/ml-venv/bin/pip install docutils pillow
set -euo pipefail

PLUSH="$(cd "$(dirname "$0")" && pwd)"
ML="$(cd "$PLUSH/../.." && pwd)"
TC="${ARM_TC:-$HOME/opt/arm-gnu-toolchain-15.3.rel2-darwin-arm64-arm-none-eabi}/bin"
export PATH="${ML_VENV:-$HOME/opt/ml-venv}/bin:$PATH"

# ML's default module list, minus lua (doesn't compile with this toolchain), plus plush
MODS="raw_video/mlv_lite raw_video/mlv_play raw_video/mlv_rec raw_video/mlv_snd file_man
      pic_view ettr dual_iso silent dot_tune autoexpo arkanoid deflick img_name bench
      selftest adv_int crop_rec dev_tools/edmac sd_clock raw_video/raw_vidx yolo plush"

echo "== stickers"
mkdir -p "$PLUSH/out"
cd "$PLUSH"
python3 -I make_cropmark.py plush.webp out/STICKER.BMP --sticker --height 288
python3 -I make_cropmark.py plush.webp out/PLUSH.BMP --preview out/preview.png

echo "== Magic Lantern 550D.109"
cd "$ML/platform/550D.109"
# macOS: use ARM's readelf and BSD stat syntax
make ARM_BINPATH="$TC" READELF="$TC/arm-none-eabi-readelf" \
     STAT_CMD='stat -f "%N: %z bytes"' ML_MODULES="$(echo $MODS)" -j8 \
     > "$PLUSH/out/build-550d.log" 2>&1 || { tail -30 "$PLUSH/out/build-550d.log"; exit 1; }

echo "== card/"
rm -rf "$PLUSH/card"
mkdir "$PLUSH/card"
cd "$PLUSH/card"
unzip -q "$ML/platform/550D.109/build/magiclantern.zip"
cp "$PLUSH/out/STICKER.BMP" ML/data/PLUSH.BMP      # for the plush module
cp "$PLUSH/out/PLUSH.BMP"   ML/cropmks/PLUSH.BMP   # crop mark version
echo "done: copy the contents of $PLUSH/card/ to the root of the SD card"
