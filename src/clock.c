/*
 * clock.c — 7Seg, a 7-segment LED clock for the desktop (GTK 4 + Cairo).
 *
 * The drawing and the GTK plumbing. What the face shows (segments, colours,
 * digits, colons) is decided in face.c.
 *
 * Build with ./build.sh from MSYS2 MINGW64, or by hand:
 *   gcc -mwindows src/clock.c src/face.c -o clock.exe $(pkg-config --cflags --libs gtk4) -lm
 */

#include <gtk/gtk.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "face.h"

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Layout constants
 * ─────────────────────────────────────────────────────────────────────────── */
#define WIN_W       720
#define WIN_H       260
/* Opacity of the halo painted under anything lit. Only the colons' halo is
 * larger than the shape on top of it, so only the colons visibly glow. */
#define GLOW_ALPHA  0.18

#define BEZEL_R     0.80
#define BEZEL_G     0.80
#define BEZEL_B     0.80

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Window state
 *
 *  One per window. Its drawing area owns it: app_state_free() runs when the
 *  area goes, and stops the tick before anything can draw into a widget that
 *  no longer exists.
 * ─────────────────────────────────────────────────────────────────────────── */
typedef struct {
    gdouble    r, g, b;
    GtkWidget *drawing_area;
    guint      tick;
} AppState;

static void app_state_free(gpointer data)
{
    AppState *state = data;
    g_clear_handle_id(&state->tick, g_source_remove);
    g_free(state);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Segment geometry (bevelled hexagons)
 * ─────────────────────────────────────────────────────────────────────────── */
static void draw_h_segment(cairo_t *cr,
                            gdouble cx, gdouble cy, gdouble len, gdouble T)
{
    gdouble bv = T * 0.45, ht = T / 2.0, hl = len / 2.0;
    cairo_move_to(cr, cx - hl + bv, cy - ht);
    cairo_line_to(cr, cx + hl - bv, cy - ht);
    cairo_line_to(cr, cx + hl,      cy      );
    cairo_line_to(cr, cx + hl - bv, cy + ht );
    cairo_line_to(cr, cx - hl + bv, cy + ht );
    cairo_line_to(cr, cx - hl,      cy      );
    cairo_close_path(cr);
}

static void draw_v_segment(cairo_t *cr,
                            gdouble cx, gdouble cy, gdouble len, gdouble T)
{
    gdouble bv = T * 0.45, ht = T / 2.0, hl = len / 2.0;
    cairo_move_to(cr, cx,      cy - hl      );
    cairo_line_to(cr, cx + ht, cy - hl + bv );
    cairo_line_to(cr, cx + ht, cy + hl - bv );
    cairo_line_to(cr, cx,      cy + hl      );
    cairo_line_to(cr, cx - ht, cy + hl - bv );
    cairo_line_to(cr, cx - ht, cy - hl + bv );
    cairo_close_path(cr);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  draw_digit
 * ─────────────────────────────────────────────────────────────────────────── */
static void draw_digit(cairo_t *cr,
                       gdouble x, gdouble y, gdouble w, gdouble h, gint digit,
                       gdouble r, gdouble g, gdouble b)
{
    if (digit < 0 || digit > 9) return;

    gdouble dr = face_dim(r);
    gdouble dg = face_dim(g);
    gdouble db = face_dim(b);

    guint   seg    = face_segments(digit);
    gdouble T      = w * 0.13;
    gdouble gap    = T * 0.55;
    gdouble slen   = w - 2.0 * gap - T;
    gdouble hh     = h / 2.0;

    gdouble hcx       = x + w / 2.0;
    gdouble top_cy    = y + gap + T / 2.0;
    gdouble mid_cy    = y + hh;
    gdouble bot_cy    = y + h - gap - T / 2.0;
    gdouble vcx_r     = x + w - gap - T / 2.0;
    gdouble vcx_l     = x + gap + T / 2.0;
    gdouble vlen      = hh - gap - T * 0.80;
    gdouble upper_vcy = y + hh / 2.0      + T * 0.10;
    gdouble lower_vcy = y + hh + hh / 2.0 - T * 0.10;

#define DRAW_SEG(on, call)                                                  \
    do {                                                                    \
        if (on) {                                                           \
            cairo_set_source_rgba(cr, r, g, b, GLOW_ALPHA);                 \
            { call; } cairo_fill(cr);                                       \
            cairo_set_source_rgb(cr, r, g, b);                              \
            { call; }                                                       \
        } else {                                                            \
            cairo_set_source_rgb(cr, dr, dg, db);                           \
            { call; }                                                       \
        }                                                                   \
        cairo_fill(cr);                                                     \
    } while (0)

    DRAW_SEG(seg & SEG_A, draw_h_segment(cr, hcx,   top_cy,    slen, T));
    DRAW_SEG(seg & SEG_B, draw_v_segment(cr, vcx_r, upper_vcy, vlen, T));
    DRAW_SEG(seg & SEG_C, draw_v_segment(cr, vcx_r, lower_vcy, vlen, T));
    DRAW_SEG(seg & SEG_D, draw_h_segment(cr, hcx,   bot_cy,    slen, T));
    DRAW_SEG(seg & SEG_E, draw_v_segment(cr, vcx_l, lower_vcy, vlen, T));
    DRAW_SEG(seg & SEG_F, draw_v_segment(cr, vcx_l, upper_vcy, vlen, T));
    DRAW_SEG(seg & SEG_G, draw_h_segment(cr, hcx,   mid_cy,    slen, T));
#undef DRAW_SEG
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  draw_colon
 * ─────────────────────────────────────────────────────────────────────────── */
static void draw_colon(cairo_t *cr,
                       gdouble x, gdouble y, gdouble w, gdouble h,
                       gboolean blink_on,
                       gdouble r, gdouble g, gdouble b)
{
    gdouble radius = w * 0.16;
    gdouble cx     = x + w / 2.0;

    if (blink_on) {
        cairo_set_source_rgba(cr, r, g, b, GLOW_ALPHA);
        cairo_arc(cr, cx, y + h * 0.32, radius * 1.6, 0, 2 * G_PI); cairo_fill(cr);
        cairo_arc(cr, cx, y + h * 0.68, radius * 1.6, 0, 2 * G_PI); cairo_fill(cr);
        cairo_set_source_rgb(cr, r, g, b);
    } else {
        cairo_set_source_rgb(cr, face_dim(r), face_dim(g), face_dim(b));
    }
    cairo_arc(cr, cx, y + h * 0.32, radius, 0, 2 * G_PI); cairo_fill(cr);
    cairo_arc(cr, cx, y + h * 0.68, radius, 0, 2 * G_PI); cairo_fill(cr);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  draw_bezel
 * ─────────────────────────────────────────────────────────────────────────── */
static void draw_bezel(cairo_t *cr,
                       gdouble x, gdouble y, gdouble w, gdouble h, gdouble rad)
{
    cairo_move_to (cr, x + rad,     y              );
    cairo_line_to (cr, x + w - rad, y              );
    cairo_arc     (cr, x + w - rad, y + rad,     rad, -G_PI/2,   0       );
    cairo_line_to (cr, x + w,       y + h - rad    );
    cairo_arc     (cr, x + w - rad, y + h - rad, rad,  0,         G_PI/2  );
    cairo_line_to (cr, x + rad,     y + h          );
    cairo_arc     (cr, x + rad,     y + h - rad, rad,  G_PI/2,    G_PI    );
    cairo_line_to (cr, x,           y + rad        );
    cairo_arc     (cr, x + rad,     y + rad,     rad,  G_PI,   3*G_PI/2   );
    cairo_close_path(cr);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  on_draw
 * ─────────────────────────────────────────────────────────────────────────── */
static void on_draw(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer user_data)
{
    (void)area;
    AppState *state = (AppState *)user_data;

    /* One reading of the clock for both the digits and the colons, so they
     * cannot disagree across a second boundary. If the time cannot be
     * broken down, every digit is drawn blank rather than as garbage. */
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    gint digits[6];
    if (!face_digits(localtime(&now.tv_sec), digits))
        for (gint i = 0; i < 6; i++) digits[i] = -1;
    gboolean blink_on = face_colons_lit(now.tv_nsec);

    const gint    N_DIGITS    = 6;
    const gint    N_COLONS    = 2;
    const gint    N_BLOCKS    = N_DIGITS + N_COLONS;
    const gdouble COLON_RATIO = 0.38;

    gint    margin    = MAX(14, (gint)(height * 0.08));
    gint    inner_gap = MAX(6,  (gint)(height * 0.04));
    gint    block_gap = MAX(4,  (gint)(height * 0.025));
    gdouble avail_w   = width  - 2 * margin;
    gdouble avail_h   = height - 2 * margin;

    gdouble denom   = N_DIGITS + N_COLONS * COLON_RATIO;
    gdouble digit_w = (avail_w - (N_BLOCKS - 1) * block_gap - 2 * inner_gap) / denom;
    if (digit_w < 8.0) digit_w = 8.0;

    gdouble colon_w = digit_w * COLON_RATIO;
    gdouble digit_h = avail_h - 2 * inner_gap;
    if (digit_h > digit_w * 2.0) digit_h = digit_w * 2.0;

    gdouble panel_w = N_DIGITS * digit_w + N_COLONS * colon_w
                      + (N_BLOCKS - 1) * block_gap + 2 * inner_gap;
    gdouble panel_h = digit_h + 2 * inner_gap;
    gdouble panel_x = (width  - panel_w) / 2.0;
    gdouble panel_y = (height - panel_h) / 2.0;

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    draw_bezel(cr, panel_x, panel_y, panel_w, panel_h, panel_h * 0.12);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, state->r, state->g, state->b, 0.12);
    cairo_set_line_width(cr, 1.2);
    draw_bezel(cr, panel_x, panel_y, panel_w, panel_h, panel_h * 0.12);
    cairo_stroke(cr);

    gdouble x      = panel_x + inner_gap;
    gdouble cell_y = panel_y + inner_gap;
    gint    di     = 0;

    for (gint block = 0; block < N_BLOCKS; block++) {
        gboolean is_colon = (block == 2 || block == 5);
        if (is_colon) {
            draw_colon(cr, x, cell_y, colon_w, digit_h, blink_on,
                       state->r, state->g, state->b);
            x += colon_w + block_gap;
        } else {
            draw_digit(cr, x, cell_y, digit_w, digit_h, digits[di++],
                       state->r, state->g, state->b);
            x += digit_w + block_gap;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Tick
 *
 *  The face changes twice a second, on the second and on the half-second,
 *  so it is redrawn just after each of those and at no other time. Each
 *  tick schedules the next from the wall clock, so the ticks cannot drift
 *  away from the changes they show.
 * ─────────────────────────────────────────────────────────────────────────── */
static gboolean on_tick(gpointer user_data);

static void schedule_tick(AppState *state)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    state->tick = g_timeout_add(face_ms_to_next_change(now.tv_nsec), on_tick, state);
}

static gboolean on_tick(gpointer user_data)
{
    AppState *state = user_data;
    gtk_widget_queue_draw(state->drawing_area);
    schedule_tick(state);
    return G_SOURCE_REMOVE;
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Action handler — "win.led-color" stateful string action
 *
 *  Called whenever the user picks a colour from Select Colors. Each window
 *  has its own, so a choice recolours the window it was made in. The action
 *  state is the colour name (e.g. "Lime").
 * ─────────────────────────────────────────────────────────────────────────── */
static void on_color_action(GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
    AppState *state = (AppState *)user_data;
    gint      i     = face_color_index(g_variant_get_string(parameter, NULL));
    if (i < 0) return;

    state->r = FACE_PALETTE[i].r / 255.0;
    state->g = FACE_PALETTE[i].g / 255.0;
    state->b = FACE_PALETTE[i].b / 255.0;
    /* Update the action's own state so the radio checkmark moves */
    g_simple_action_set_state(action, parameter);
    gtk_widget_queue_draw(state->drawing_area);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  build_menu_model
 *
 *  Constructs the GMenuModel tree:
 *
 *    Select Colors ► Lime
 *                    Amber
 *                    Red
 *                    ...
 * ─────────────────────────────────────────────────────────────────────────── */
static GMenuModel *build_menu_model(void)
{
    /* ── Select Colors menu (radio group) ───────────────────────────────── */
    GMenu *color_menu = g_menu_new();
    for (gint i = 0; i < FACE_N_COLORS; i++) {
        GMenuItem *item = g_menu_item_new(FACE_PALETTE[i].name, NULL);
        g_menu_item_set_action_and_target_value(
            item,
            "win.led-color",
            g_variant_new_string(FACE_PALETTE[i].name));
        g_menu_append_item(color_menu, item);
        g_object_unref(item);
    }

    /* ── Top-level menu bar ──────────────────────────────────────────────── */
    GMenu *menubar = g_menu_new();
    g_menu_append_submenu(menubar, "Select Colors", G_MENU_MODEL(color_menu));

    g_object_unref(color_menu);

    return G_MENU_MODEL(menubar);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  add_bundled_icons
 *
 *  build.sh puts the window icon in share/icons beside clock.exe. GLib looks
 *  in that share folder by default, but not when XDG_DATA_DIRS is set, and
 *  an MSYS2 shell sets it; so the folder is named to GTK outright, once.
 * ─────────────────────────────────────────────────────────────────────────── */
static void add_bundled_icons(GtkWidget *window)
{
#ifdef G_OS_WIN32
    static gboolean added = FALSE;
    if (added) return;
    added = TRUE;

    gchar *base  = g_win32_get_package_installation_directory_of_module(NULL);
    gchar *icons = g_build_filename(base, "share", "icons", NULL);
    gtk_icon_theme_add_search_path(
        gtk_icon_theme_get_for_display(gtk_widget_get_display(window)), icons);
    g_free(icons);
    g_free(base);
#else
    (void)window;
#endif
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  on_activate
 * ─────────────────────────────────────────────────────────────────────────── */
static void on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    /* ── App state, in the default colour ───────────────────────────────── */
    const FaceColor *initial = &FACE_PALETTE[FACE_DEFAULT_COLOR];
    AppState *state = g_new0(AppState, 1);
    state->r = initial->r / 255.0;
    state->g = initial->g / 255.0;
    state->b = initial->b / 255.0;

    /* ── app.quit, once: a second launch activates this process again ──── */
    if (!g_action_map_lookup_action(G_ACTION_MAP(app), "quit")) {
        GSimpleAction *quit_action = g_simple_action_new("quit", NULL);
        g_signal_connect_swapped(quit_action, "activate",
                                 G_CALLBACK(g_application_quit), app);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(quit_action));
        g_object_unref(quit_action);
    }

    /* ── Drawing area, which owns the state ─────────────────────────────── */
    GtkWidget *area = gtk_drawing_area_new();
    state->drawing_area = area;
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, state,
                                   app_state_free);
    schedule_tick(state);

    /* ── Menu bar widget ─────────────────────────────────────────────────── */
    GMenuModel *menu_model = build_menu_model();
    GtkWidget  *menubar    = gtk_popover_menu_bar_new_from_model(menu_model);
    g_object_unref(menu_model);

    /* ── Root layout: menu bar on top, clock canvas below ───────────────── */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(vbox), menubar);
    gtk_box_append(GTK_BOX(vbox), area);
    gtk_widget_set_vexpand(area, TRUE);

    /* ── Window ──────────────────────────────────────────────────────────── */
    GtkWidget *window = gtk_application_window_new(app);

    /* win.led-color  (string radio, initially the default colour) */
    GSimpleAction *color_action =
        g_simple_action_new_stateful("led-color",
                                     G_VARIANT_TYPE_STRING,
                                     g_variant_new_string(initial->name));
    g_signal_connect(color_action, "activate",
                     G_CALLBACK(on_color_action), state);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(color_action));
    g_object_unref(color_action);

    gtk_window_set_title       (GTK_WINDOW(window), "LED Clock");
    gtk_window_set_default_size(GTK_WINDOW(window), WIN_W, WIN_H);
    gtk_window_set_resizable   (GTK_WINDOW(window), TRUE);
    gtk_window_set_child       (GTK_WINDOW(window), vbox);

    add_bundled_icons(window);
    gtk_window_set_icon_name(GTK_WINDOW(window), "com.example.ledclock");

    gtk_widget_set_visible(window, TRUE);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  main
 * ─────────────────────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    GtkApplication *app =
        gtk_application_new("com.example.ledclock", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}