/*
 * app_test.c — the clock as an application: real windows, real menus, the
 * real main loop. Windows appear on screen for a few seconds while it runs.
 *
 * clock.c is included so its on_activate() can be connected to a test
 * application. Activating it twice is what launching the clock a second
 * time does: GApplication hands the launch to the copy already running,
 * which opens a second window in the same process.
 *
 * Checks named "regression:" fail against the first commit's clock.c
 * (tests/run.sh --original).
 */
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define main clock_main
#include CLOCK_C
#undef main

static int failures, checks;

static void check(int ok, const char *name, const char *detail)
{
    checks++;
    printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", name,
           detail && detail[0] ? "  -- " : "", detail ? detail : "");
    fflush(stdout);
    if (!ok) failures++;
}

/* ── Counting GLib's complaints ─────────────────────────────────────────── */

static int complaints;
static char first_complaint[512];

static GLogWriterOutput count_writer(GLogLevelFlags level, const GLogField *fields,
                                     gsize n, gpointer data)
{
    if (level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
        if (complaints++ == 0)
            for (gsize i = 0; i < n; i++)
                if (strcmp(fields[i].key, "MESSAGE") == 0)
                    g_strlcpy(first_complaint, fields[i].value, sizeof first_complaint);
    }
    return g_log_writer_default(level, fields, n, data);
}

/* ── Running the main loop for a while ──────────────────────────────────── */

static gboolean set_flag(gpointer flag) { *(gboolean *)flag = TRUE; return G_SOURCE_REMOVE; }

static void pump(guint ms)
{
    gboolean done = FALSE;
    g_timeout_add(ms, set_flag, &done);
    while (!done) g_main_context_iteration(NULL, TRUE);
}

/* ── Finding things in a clock window ───────────────────────────────────── */

static GtkWidget *menubar_of(GtkWindow *w)
{
    return gtk_widget_get_first_child(gtk_window_get_child(w));
}

static GtkWidget *area_of(GtkWindow *w)
{
    return gtk_widget_get_last_child(gtk_window_get_child(w));
}

/* Choose a colour the way a click does: the item's own action and target,
 * activated from the menu bar of the window it was clicked in. */
static gboolean choose_colour(GtkWindow *w, const char *label)
{
    GtkWidget  *bar   = menubar_of(w);
    GMenuModel *top   = gtk_popover_menu_bar_get_menu_model(GTK_POPOVER_MENU_BAR(bar));
    GMenuModel *menu  = g_menu_model_get_item_link(top, 0, G_MENU_LINK_SUBMENU);
    gboolean    found = FALSE;

    for (int i = 0; menu && i < g_menu_model_get_n_items(menu) && !found; i++) {
        char *text = NULL, *action = NULL;
        g_menu_model_get_item_attribute(menu, i, G_MENU_ATTRIBUTE_LABEL, "s", &text);
        g_menu_model_get_item_attribute(menu, i, G_MENU_ATTRIBUTE_ACTION, "s", &action);
        if (g_strcmp0(text, label) == 0 && action) {
            GVariant *target = g_menu_model_get_item_attribute_value(
                menu, i, G_MENU_ATTRIBUTE_TARGET, NULL);
            found = gtk_widget_activate_action_variant(bar, action, target);
            if (target) g_variant_unref(target);
        }
        g_free(text);
        g_free(action);
    }
    g_clear_object(&menu);
    return found;
}

/* Count the pixels of a window's clock face that are exactly one colour,
 * or -1 if it has no size yet. The face is drawn afresh by the drawing
 * area's own snapshot method, which runs its draw function with the state
 * it was given; GtkWidgetPaintable was tried first and, about one run in
 * five, returned nothing for a window that was mapped and focused. */
static long pixels_of(GtkWindow *w, guint8 r, guint8 g, guint8 b)
{
    GtkWidget *area = area_of(w);
    int width = gtk_widget_get_width(area), height = gtk_widget_get_height(area);
    if (width <= 0 || height <= 0) return -1;

    GtkSnapshot *snapshot = gtk_snapshot_new();
    GTK_WIDGET_GET_CLASS(area)->snapshot(area, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    if (!node) return -1;

    GskRenderer *renderer = gsk_cairo_renderer_new();
    gsk_renderer_realize_for_display(renderer, gdk_display_get_default(), NULL);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    GdkTexture *texture = gsk_renderer_render_texture(renderer, node, &bounds);

    int tw = gdk_texture_get_width(texture), th = gdk_texture_get_height(texture);
    guchar *buf = g_malloc((gsize)tw * th * 4);
    gdk_texture_download(texture, buf, (gsize)tw * 4);   /* B G R A, premultiplied */

    long count = 0;
    for (long i = 0; i < (long)tw * th; i++) {
        const guchar *p = buf + i * 4;
        if (p[3] == 255 && p[2] == r && p[1] == g && p[0] == b) count++;
    }

    g_free(buf);
    g_object_unref(texture);
    gsk_renderer_unrealize(renderer);
    g_object_unref(renderer);
    gsk_render_node_unref(node);
    return count;
}

#define RED     0xE8, 0x20, 0x20
#define PURPLE  0xA0, 0x40, 0xF0

/* A window can take more than a second to be laid out. */
static gboolean wait_for_face(GtkWindow *w)
{
    for (int i = 0; i < 100; i++) {
        if (pixels_of(w, 0, 0, 0) >= 0) return TRUE;
        pump(100);
    }
    return FALSE;
}

/* ── Counting repaints ──────────────────────────────────────────────────── */

#define MAX_PAINTS 256
#define HALF_SECOND_NS 500000000LL

static int       paints;
static long long paint_ns[MAX_PAINTS];   /* wall clock at each repaint */

static void on_after_paint(GdkFrameClock *clock, gpointer data)
{
    (void)clock; (void)data;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    if (paints < MAX_PAINTS)
        paint_ns[paints] = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
    paints++;
}

static void count_paints(GtkWindow *w, guint ms)
{
    GdkFrameClock *clock = gtk_widget_get_frame_clock(GTK_WIDGET(w));
    paints = 0;
    gulong id = g_signal_connect(clock, "after-paint", G_CALLBACK(on_after_paint), NULL);
    pump(ms);
    g_signal_handler_disconnect(clock, id);
}

/* The latest a repaint came after the half-second before it, in ms. */
static long worst_lag_ms(void)
{
    long long worst = 0;
    for (int i = 0; i < paints && i < MAX_PAINTS; i++) {
        long long lag = paint_ns[i] % HALF_SECOND_NS;
        if (lag > worst) worst = lag;
    }
    return (long)(worst / 1000000);
}

/* Half-seconds wholly inside the count with no repaint in them. */
static int half_seconds_missed(void)
{
    if (paints < 2 || paints > MAX_PAINTS) return -1;
    long long first = paint_ns[0] / HALF_SECOND_NS + 1;
    long long last  = paint_ns[paints - 1] / HALF_SECOND_NS - 1;
    int missed = 0;
    for (long long slot = first; slot <= last; slot++) {
        int seen = 0;
        for (int i = 0; i < paints && !seen; i++)
            seen = paint_ns[i] / HALF_SECOND_NS == slot;
        missed += !seen;
    }
    return missed;
}

static GtkWindow *other_than(GtkApplication *app, GtkWindow *w)
{
    for (GList *l = gtk_application_get_windows(app); l; l = l->next)
        if (l->data != w) return l->data;
    return NULL;
}

int main(void)
{
    char detail[600];
    g_log_set_writer_func(count_writer, NULL, NULL);

    GtkApplication *app = gtk_application_new("com.example.ledclock.apptest",
                                              G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    if (!g_application_register(G_APPLICATION(app), NULL, NULL)) {
        check(0, "the test application registers", NULL);
        return 1;
    }

    /* ── One window ─────────────────────────────────────────────────────── */
    g_application_activate(G_APPLICATION(app));
    pump(300);
    GtkWindow *first = gtk_application_get_active_window(app);
    if (!first && gtk_application_get_windows(app))
        first = gtk_application_get_windows(app)->data;
    check(first != NULL, "launching opens a window", NULL);
    if (!first) return 1;
    check(wait_for_face(first), "and draws a clock face in it", NULL);

    long purple = pixels_of(first, PURPLE);
    snprintf(detail, sizeof detail, "%ld purple pixels", purple);
    check(purple > 1000, "a new window is purple", detail);

    /* ── A second launch ────────────────────────────────────────────────── */
    g_application_activate(G_APPLICATION(app));
    pump(300);
    GtkWindow *second = other_than(app, first);
    check(second != NULL, "launching again opens a second window", NULL);
    if (!second) return 1;
    check(wait_for_face(second), "and draws a clock face in it", NULL);

    check(choose_colour(first, "Red"), "the first window's menu offers Red", NULL);
    pump(500);

    long first_red = pixels_of(first, RED), first_purple = pixels_of(first, PURPLE);
    long second_red = pixels_of(second, RED), second_purple = pixels_of(second, PURPLE);

    snprintf(detail, sizeof detail, "first window: %ld red, %ld purple pixels",
             first_red, first_purple);
    check(first_red > 1000 && first_purple == 0,
          "regression: Red chosen in the first window turns the first window red", detail);

    snprintf(detail, sizeof detail, "second window: %ld red, %ld purple pixels",
             second_red, second_purple);
    check(second_red == 0 && second_purple > 1000,
          "regression: and leaves the second window as it was", detail);

    /* ── Closing one of the two ─────────────────────────────────────────── */
    complaints = 0;
    first_complaint[0] = '\0';
    gtk_window_destroy(first);
    pump(1500);
    snprintf(detail, sizeof detail, "%d warnings or criticals; the first: %s",
             complaints, first_complaint[0] ? first_complaint : "none");
    check(complaints == 0,
          "regression: closing one window leaves nothing drawing into it", detail);

    /* ── An idle window, left alone for three seconds ───────────────────── */
    count_paints(second, 3000);
    int missed = half_seconds_missed();
    snprintf(detail, sizeof detail, "%d repaints, %d half-seconds without one", paints, missed);
    check(missed == 0, "the other window keeps ticking, every half-second", detail);

    snprintf(detail, sizeof detail, "%d repaints in 3 s", paints);
    check(paints <= 9, "regression: an idle clock repaints no more than it changes", detail);

    long lag = worst_lag_ms();
    snprintf(detail, sizeof detail, "the latest came %ld ms after a half-second", lag);
    check(lag <= 60, "regression: and each repaint comes just after the change it shows",
          detail);

    /* ── Closing the last ───────────────────────────────────────────────── */
    complaints = 0;
    gtk_window_destroy(second);
    pump(300);
    check(gtk_application_get_windows(app) == NULL && complaints == 0,
          "regression: closing the last window leaves none and no complaints", NULL);

    g_object_unref(app);
    printf("\n%s: %d of %d checks failed\n", failures ? "FAILED" : "passed",
           failures, checks);
    return failures ? 1 : 0;
}
