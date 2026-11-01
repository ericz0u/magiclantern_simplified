/* Plush: draws a sticker (ML/DATA/PLUSH.BMP) on top of every screen,
 * at a random position and size each time the screen changes.
 *
 * Canon redraws its screens whenever it likes, so a task keeps repainting
 * any sticker pixels that got overwritten. It also remembers what was under
 * the sticker, so the old one can be removed when the screen changes in place
 * (e.g. INFO in LiveView, where nothing else would clear it).
 *
 * The sticker is an 8-bit BMP using the camera's palette, with index 0 as
 * transparent; make it with make_cropmark.py --sticker --height 288.
 *
 * Canon swaps parts of the palette between screens, so the sticker only uses
 * entries that stay put, plus its own colors in CUSTOM_FIRST..CUSTOM_LAST
 * (unused by Canon and ML). Their RGB comes from the BMP's palette table and
 * gets programmed into the camera's palette on every tick, since Canon
 * reloads the palette when it switches screens.
 */

#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <menu.h>
#include <config.h>
#include <propvalues.h>
#include <dialog.h>

#define STICKER_PATH "ML/DATA/PLUSH.BMP"

/* 550D: 3" 3:2 LCD, so 480 lines cover about 1.66" */
#define PX_PER_INCH 288
#define MIN_H (PX_PER_INCH * 3 / 10)
#define MAX_H PX_PER_INCH

#define SCREEN_W 720
#define SCREEN_H 480

#define TICK_MS 100         /* how often overwritten pixels get repainted */
#define SETTLE_MS 150       /* give Canon a moment to draw the new screen */

/* palette entries the sticker may define itself */
#define CUSTOM_FIRST 0x1A
#define CUSTOM_LAST  0x25

extern int get_gui_mode();
extern int display_is_on();
extern int is_play_mode();
extern int is_menu_mode();

static CONFIG_INT("plush.enabled", plush_enabled, 1);

static struct bmp_file_t * sticker = 0;
static int sticker_stride;

/* camera palette values (opacity, Y, U, V) for the custom entries the sticker uses */
static uint32_t custom_palette[CUSTOM_LAST + 1];
static uint8_t custom_used[CUSTOM_LAST + 1];
static int custom_palette_applied = 0;

/* current placement; cur_w == 0 means nothing on screen */
static int cur_x, cur_y, cur_w, cur_h;
static uint8_t * drawn = 0;  /* scaled sticker, cur_w x cur_h, 0 = transparent */
static uint8_t * under = 0;  /* screen contents below each sticker pixel */

static volatile int reshuffle = 0;

static uint32_t rng_state;

static uint32_t rng()
{
    /* xorshift32 */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int rand_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + rng() % (hi - lo + 1);
}

/* BMP palette RGB -> camera palette entry; inverse of yuv2rgb in src/imgconv.c */
static uint32_t rgb_to_palette_entry(int r, int g, int b, int opacity)
{
    int y = (218 * r + 732 * g + 74 * b) >> 10;
    int u = COERCE((b - y) * 1024 / 1900, -127, 127);
    int v = COERCE((r - y) * 1024 / 1608, -127, 127);
    return ((opacity & 0xFF) << 24) | ((y & 0xFF) << 16) | ((u & 0xFF) << 8) | (v & 0xFF);
}

/* read the sticker's own colors from its BMP palette table (BGRA after the header) */
static void load_custom_palette(int * used)
{
    /* same opacity as Canon's opaque white */
    int opacity = (LCD_Palette[3 * COLOR_WHITE + 2] >> 24) & 0xFF;
    uint8_t * bgra = (uint8_t *) sticker + 14 + sticker->hdr_size;

    for (int i = CUSTOM_FIRST; i <= CUSTOM_LAST; i++)
    {
        if (!used[i]) continue;
        uint8_t * c = bgra + 4 * i;
        custom_palette[i] = rgb_to_palette_entry(c[2], c[1], c[0], opacity);
        custom_used[i] = 1;
    }
}

static void apply_custom_palette()
{
    for (int i = CUSTOM_FIRST; i <= CUSTOM_LAST; i++)
    {
        if (!custom_used[i]) continue;
        EngDrvOut(LCD_Palette[3 * i], custom_palette[i]);
        EngDrvOut(LCD_Palette[3 * i + 0x300], custom_palette[i]);
    }
    custom_palette_applied = 1;
}

static void restore_custom_palette()
{
    for (int i = CUSTOM_FIRST; i <= CUSTOM_LAST; i++)
    {
        if (!custom_used[i]) continue;
        EngDrvOut(LCD_Palette[3 * i], LCD_Palette[3 * i + 2]);
        EngDrvOut(LCD_Palette[3 * i + 0x300], LCD_Palette[3 * i + 2]);
    }
    custom_palette_applied = 0;
}

/* changes whenever Canon or ML switches to a different screen */
static int screen_signature()
{
    /* turning the mode dial (P/Tv/Av/...) keeps the same dialog, but the
     * shooting screens show the mode, so count it there */
    int shows_mode = !is_play_mode() && !is_menu_mode() && !gui_menu_shown();

    return (int)(intptr_t)get_current_dialog_handler()
         + get_gui_mode() * 1009
         + gui_state * 7919
         + lv * 104729
         + lv_dispsize * 1299709
         + gui_menu_shown() * 15485863
         + display_is_on() * 32452843
         + (shows_mode ? shooting_mode_custom * 49979687 : 0);
}

/* remove the sticker, but only where it is still on screen untouched;
 * if most of it was already painted over, the screen was redrawn anyway */
static void erase_sticker()
{
    if (!cur_w) return;

    uint8_t * bvram = bmp_vram();
    if (bvram)
    {
        int opaque = 0, intact = 0;
        for (int y = 0; y < cur_h; y++)
        {
            uint8_t * row = bvram + (cur_y + y) * BMPPITCH + cur_x;
            for (int x = 0; x < cur_w; x++)
            {
                uint8_t c = drawn[y * cur_w + x];
                if (!c) continue;
                opaque++;
                if (row[x] == c) intact++;
            }
        }

        if (intact * 10 >= opaque * 9)
        {
            for (int y = 0; y < cur_h; y++)
            {
                uint8_t * row = bvram + (cur_y + y) * BMPPITCH + cur_x;
                for (int x = 0; x < cur_w; x++)
                {
                    int i = y * cur_w + x;
                    if (drawn[i] && row[x] == drawn[i])
                    {
                        row[x] = under[i];
                    }
                }
            }
        }
    }

    cur_w = 0;
}

static void place_sticker()
{
    uint8_t * bvram = bmp_vram();
    if (!bvram) return;

    int h = rand_range(MIN_H, MAX_H);
    int w = sticker->width * h / sticker->height;
    int x0 = rand_range(0, SCREEN_W - w);
    int y0 = rand_range(0, SCREEN_H - h);

    /* nearest-neighbor downscale; BMP rows are stored bottom-up */
    for (int y = 0; y < h; y++)
    {
        int sy = y * sticker->height / h;
        uint8_t * src = sticker->image + (sticker->height - 1 - sy) * sticker_stride;
        uint8_t * row = bvram + (y0 + y) * BMPPITCH + x0;
        for (int x = 0; x < w; x++)
        {
            int i = y * w + x;
            drawn[i] = src[x * sticker->width / w];
            under[i] = row[x];
        }
    }

    cur_x = x0; cur_y = y0; cur_w = w; cur_h = h;
}

/* repaint sticker pixels that something else drew over */
static void repaint_sticker()
{
    if (!cur_w) return;

    uint8_t * bvram = bmp_vram();
    if (!bvram) return;

    for (int y = 0; y < cur_h; y++)
    {
        uint8_t * row = bvram + (cur_y + y) * BMPPITCH + cur_x;
        for (int x = 0; x < cur_w; x++)
        {
            int i = y * cur_w + x;
            uint8_t c = drawn[i];
            if (c && row[x] != c)
            {
                under[i] = row[x];
                row[x] = c;
            }
        }
    }
}

static void plush_task()
{
    int prev_sig = 0;

    TASK_LOOP
    {
        msleep(TICK_MS);

        if (!plush_enabled || !display_is_on())
        {
            if (cur_w) BMP_LOCK( erase_sticker(); )
            if (custom_palette_applied && display_is_on()) restore_custom_palette();
            prev_sig = 0;
            continue;
        }

        apply_custom_palette();

        int sig = screen_signature();
        if (sig != prev_sig || reshuffle || !cur_w)
        {
            prev_sig = sig;
            reshuffle = 0;
            BMP_LOCK( erase_sticker(); )
            msleep(SETTLE_MS);
            BMP_LOCK( place_sticker(); )
        }

        BMP_LOCK( repaint_sticker(); )
    }
}

static MENU_SELECT_FUNC(plush_shuffle)
{
    reshuffle = 1;
}

static MENU_UPDATE_FUNC(plush_update)
{
    if (!sticker)
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Could not load " STICKER_PATH);
    }
}

static struct menu_entry plush_menu[] =
{
    {
        .name       = "Plush",
        .priv       = &plush_enabled,
        .max        = 1,
        .update     = plush_update,
        .help       = "Shows a plush on every screen, moving each time the screen changes.",
        .children   = (struct menu_entry[])
        {
            {
                .name   = "Shuffle now",
                .select = plush_shuffle,
                .help   = "Move the plush to a new random spot and size.",
            },
            MENU_EOL,
        },
    },
};

static unsigned int plush_keypress(unsigned int key)
{
    /* INFO changes the screen layout without changing dialog */
    if (key == MODULE_KEY_INFO)
    {
        reshuffle = 1;
    }
    return 1;
}

static unsigned int plush_init()
{
    menu_add("Overlay", plush_menu, COUNT(plush_menu));

    struct bmp_file_t * bmp = bmp_load(STICKER_PATH, 0);
    if (!bmp || bmp->bits_per_pixel != 8 || bmp->compression != 0 ||
        bmp->width == 0 || bmp->height <= 0)
    {
        printf("plush: could not load %s\n", STICKER_PATH);
        if (bmp) free(bmp);
        return 0;
    }

    int max_w = MIN((int)bmp->width * MAX_H / bmp->height, SCREEN_W);
    drawn = malloc(max_w * MAX_H);
    under = malloc(max_w * MAX_H);
    if (!drawn || !under)
    {
        printf("plush: out of memory\n");
        if (drawn) free(drawn);
        if (under) free(under);
        free(bmp);
        return 0;
    }

    sticker = bmp;
    sticker_stride = (sticker->width + 3) & ~3;
    rng_state = get_ms_clock() | 1;

    int used[256] = {0};
    for (int y = 0; y < sticker->height; y++)
    {
        for (int x = 0; x < (int)sticker->width; x++)
        {
            used[sticker->image[y * sticker_stride + x]] = 1;
        }
    }
    load_custom_palette(used);

    task_create("plush_task", 0x1e, 0x1000, plush_task, 0);
    return 0;
}

static unsigned int plush_deinit()
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(plush_init)
    MODULE_DEINIT(plush_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS, plush_keypress, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(plush_enabled)
MODULE_CONFIGS_END()
