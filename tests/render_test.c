/*
 * render_test.c — the clock face, drawn off screen at fixed times and
 * compared pixel for pixel with what the first commit drew.
 *
 * The images in tests/golden/ were drawn by the original clock.c, through
 * this same file (tests/make_golden.sh). The face is meant to look exactly as
 * it did, so every case below has to match exactly: same digits, same colour,
 * same geometry, same anti-aliasing.
 *
 * clock.c is included rather than linked, so its static on_draw() can be
 * called directly. Its clock is replaced by a fixed instant, and localtime()
 * by gmtime(), so a case means the same thing in any time zone.
 *
 *   render_test GOLDEN_DIR            compare
 *   render_test GOLDEN_DIR --write    (re)draw the goldens
 */
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static time_t fake_sec;
static long   fake_nsec;

/* The original read time() as well as clock_gettime(). */
G_GNUC_UNUSED static time_t fake_time(time_t *out)
{
    if (out) *out = fake_sec;
    return fake_sec;
}

static int fake_clock_gettime(int clock_id, struct timespec *ts)
{
    (void)clock_id;
    ts->tv_sec  = fake_sec;
    ts->tv_nsec = fake_nsec;
    return 0;
}

#define time          fake_time
#define clock_gettime fake_clock_gettime
#define localtime     gmtime
#define main          clock_main
#include CLOCK_C
#undef main
#undef localtime
#undef clock_gettime
#undef time

/* The palette, restated here so the test does not read it from the code
 * under test. */
static const struct { const char *name; int r, g, b; } COLOURS[] = {
    { "lime",   0x16, 0xC6, 0x0C }, { "amber", 0xFF, 0x8C, 0x00 },
    { "red",    0xE8, 0x20, 0x20 }, { "teal",  0x00, 0x8B, 0xA8 },
    { "navy",   0x18, 0x4C, 0xD0 }, { "pink",  0xFF, 0x30, 0x80 },
    { "purple", 0xA0, 0x40, 0xF0 }, { "gold",  0xC8, 0x9A, 0x00 },
};
enum { LIME, AMBER, RED, TEAL, NAVY, PINK, PURPLE, GOLD };

typedef struct {
    const char *label;
    int hh, mm, ss; long ms;
    int colour;
    int width, height;
} Case;

static const Case CASES[] = {
    /* The default window's drawing area, colons lit and unlit. Between
     * them these show every digit. */
    { "012345-lit",       1, 23, 45, 250, PURPLE, 720, 235 },
    { "165708-unlit",    16, 57,  8, 750, PURPLE, 720, 235 },
    { "235959-unlit",    23, 59, 59, 999, PURPLE, 720, 235 },
    { "000000-lit",       0,  0,  0,   0, PURPLE, 720, 235 },
    { "half-second",     12, 34, 56, 500, PURPLE, 720, 235 },
    /* Every colour. */
    { "lime",            19, 46, 32, 100, LIME,   360, 120 },
    { "amber",           19, 46, 32, 100, AMBER,  360, 120 },
    { "red",             19, 46, 32, 100, RED,    360, 120 },
    { "teal",            19, 46, 32, 100, TEAL,   360, 120 },
    { "navy",            19, 46, 32, 100, NAVY,   360, 120 },
    { "pink",            19, 46, 32, 100, PINK,   360, 120 },
    { "gold",            19, 46, 32, 100, GOLD,   360, 120 },
    /* Wide and short (height decides), tall and narrow (width decides). */
    { "wide",            10,  8,  6, 200, AMBER, 1400, 300 },
    { "tall",            10,  8,  6, 200, TEAL,   300, 600 },
    /* Smaller than the digits can be drawn: clipped, never an error. */
    { "tiny",             8,  8,  8, 200, RED,     60,  30 },
    { "sliver",           8,  8,  8, 200, RED,     20,   5 },
};

static int failures;

static void check(int ok, const char *name, const char *detail)
{
    printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", name,
           detail[0] ? "  -- " : "", detail);
    if (!ok) failures++;
}

static cairo_surface_t *draw_case(const Case *c, cairo_status_t *status)
{
    fake_sec  = (time_t)(c->hh * 3600 + c->mm * 60 + c->ss) + 86400 * 20000;
    fake_nsec = c->ms * 1000000L;

    AppState state;
    memset(&state, 0, sizeof state);
    state.r = COLOURS[c->colour].r / 255.0;
    state.g = COLOURS[c->colour].g / 255.0;
    state.b = COLOURS[c->colour].b / 255.0;

    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                    c->width, c->height);
    cairo_t *cr = cairo_create(s);
    on_draw(NULL, cr, c->width, c->height, &state);
    *status = cairo_status(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

/* Count differing pixels; RGB24 leaves the top byte undefined. */
static long compare(cairo_surface_t *a, cairo_surface_t *b, int *fx, int *fy)
{
    int w = cairo_image_surface_get_width(a), h = cairo_image_surface_get_height(a);
    if (w != cairo_image_surface_get_width(b) || h != cairo_image_surface_get_height(b))
        return -1;
    long diff = 0;
    *fx = *fy = -1;
    for (int y = 0; y < h; y++) {
        const guint32 *ra = (const guint32 *)(cairo_image_surface_get_data(a) + y * cairo_image_surface_get_stride(a));
        const guint32 *rb = (const guint32 *)(cairo_image_surface_get_data(b) + y * cairo_image_surface_get_stride(b));
        for (int x = 0; x < w; x++) {
            if ((ra[x] & 0xFFFFFF) != (rb[x] & 0xFFFFFF)) {
                if (diff == 0) { *fx = x; *fy = y; }
                diff++;
            }
        }
    }
    return diff;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: render_test GOLDEN_DIR [--write]\n");
        return 2;
    }
    const char *dir = argv[1];
    gboolean write = argc > 2 && strcmp(argv[2], "--write") == 0;

    for (size_t i = 0; i < G_N_ELEMENTS(CASES); i++) {
        const Case *c = &CASES[i];
        char path[1024], name[128], detail[1100] = "";
        snprintf(path, sizeof path, "%s/%s.png", dir, c->label);
        snprintf(name, sizeof name, "face %s (%dx%d)", c->label, c->width, c->height);

        cairo_status_t status;
        cairo_surface_t *got = draw_case(c, &status);

        if (write) {
            check(status == CAIRO_STATUS_SUCCESS &&
                  cairo_surface_write_to_png(got, path) == CAIRO_STATUS_SUCCESS,
                  name, path);
        } else {
            if (status != CAIRO_STATUS_SUCCESS)
                snprintf(detail, sizeof detail, "cairo: %s", cairo_status_to_string(status));
            check(status == CAIRO_STATUS_SUCCESS, name, detail);

            snprintf(name, sizeof name, "face %s matches the original", c->label);
            cairo_surface_t *want = cairo_image_surface_create_from_png(path);
            if (cairo_surface_status(want) != CAIRO_STATUS_SUCCESS) {
                snprintf(detail, sizeof detail, "cannot read %s", path);
                check(0, name, detail);
            } else {
                int fx, fy;
                long diff = compare(got, want, &fx, &fy);
                if (diff < 0)
                    snprintf(detail, sizeof detail, "golden is a different size");
                else if (diff > 0)
                    snprintf(detail, sizeof detail, "%ld pixels differ, first at (%d,%d)", diff, fx, fy);
                check(diff == 0, name, detail);
            }
            cairo_surface_destroy(want);
        }
        cairo_surface_destroy(got);
    }

    printf("\n%s: %d failure%s\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
