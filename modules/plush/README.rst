Plush
=====

Draws a plush (or any sticker) on top of every screen, at a random position
and size (0.3" to 1" tall on a 550D) each time the screen changes.

Put the sticker at ML/DATA/PLUSH.BMP. It must be an uncompressed 8-bit BMP
using the camera's palette, with color 0 as transparent. Make one with
make_cropmark.py --sticker --height 288 (in this module's folder).

Canon redraws its own screens on its own schedule, so the plush may flicker
briefly on Canon menus and playback before it is repainted.

:Author: ericzou
:License: GPL
:Summary: Plush sticker on every screen
