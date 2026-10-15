/*
 * face_test.c — the clock-face decisions in src/face.c, with no GTK.
 *
 * The expected segments are written out as letters, the way a datasheet
 * gives them, rather than as the bit table under test.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../src/face.h"

static int failures, checks;

static void check(int ok, const char *name)
{
    checks++;
    printf("%s  %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) failures++;
}

static unsigned letters(const char *s)
{
    unsigned bits = 0;
    for (; *s; s++) bits |= 1u << (*s - 'a');
    return bits;
}

static void test_segments(void)
{
    static const char *const LIT[10] = {
        "abcdef", "bc", "abdeg", "abcdg", "bcfg",
        "acdfg", "acdefg", "abc", "abcdefg", "abcdfg"
    };
    char name[64];
    for (int d = 0; d < 10; d++) {
        snprintf(name, sizeof name, "digit %d lights %s", d, LIT[d]);
        check(face_segments(d) == letters(LIT[d]), name);
    }
    check(SEG_A == letters("a") && SEG_D == letters("d") && SEG_G == letters("g"),
          "SEG_ constants are the a-g bits");
    check(face_segments(-1) == 0, "digit -1 lights nothing");
    check(face_segments(10) == 0, "digit 10 lights nothing");
    check(face_segments('5') == 0, "the character '5' is not the digit 5");
}

static void test_palette(void)
{
    static const char *const NAMES[FACE_N_COLORS] = {
        "Lime", "Amber", "Red", "Teal", "Navy", "Pink", "Purple", "Gold"
    };
    char name[64];
    for (int i = 0; i < FACE_N_COLORS; i++) {
        snprintf(name, sizeof name, "palette entry %d is %s", i, NAMES[i]);
        check(FACE_PALETTE[i].name && strcmp(FACE_PALETTE[i].name, NAMES[i]) == 0, name);
        snprintf(name, sizeof name, "%s is found at index %d", NAMES[i], i);
        check(face_color_index(NAMES[i]) == i, name);
    }
    check(strcmp(FACE_PALETTE[FACE_DEFAULT_COLOR].name, "Purple") == 0,
          "the default colour is Purple");
    check(FACE_PALETTE[FACE_DEFAULT_COLOR].r == 0xA0 &&
          FACE_PALETTE[FACE_DEFAULT_COLOR].g == 0x40 &&
          FACE_PALETTE[FACE_DEFAULT_COLOR].b == 0xF0, "Purple is A040F0");
    check(face_color_index("purple") == -1, "colour names are exact, not case-folded");
    check(face_color_index("") == -1, "an empty name is no colour");
    check(face_color_index(NULL) == -1, "NULL is no colour");
}

static void test_dim(void)
{
    int all_floor = 1;
    for (int i = 0; i < FACE_N_COLORS; i++) {
        const FaceColor *c = &FACE_PALETTE[i];
        all_floor &= face_dim(c->r / 255.0) == FACE_DIM_FLOOR &&
                     face_dim(c->g / 255.0) == FACE_DIM_FLOOR &&
                     face_dim(c->b / 255.0) == FACE_DIM_FLOOR;
    }
    check(all_floor, "every colour's unlit segments are the same light grey");
    check(face_dim(0.0) == 0.96 && face_dim(1.0) == 0.96, "the grey is 0.96");
    check(face_dim(20.0) == 2.0, "above the floor, unlit is a tenth of lit");
}

static struct tm at(int h, int m, int s)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = s;
    return tm;
}

static int reads(int h, int m, int s, const char *want)
{
    struct tm tm = at(h, m, s);
    int d[6];
    if (!face_digits(&tm, d)) return 0;
    for (int i = 0; i < 6; i++)
        if (d[i] != want[i] - '0') return 0;
    return 1;
}

static void test_digits(void)
{
    check(reads(0, 0, 0, "000000"), "midnight reads 000000");
    check(reads(9, 5, 7, "090507"), "single figures are zero-padded");
    check(reads(23, 59, 59, "235959"), "the last second of the day reads 235959");
    check(reads(12, 34, 56, "123456"), "12:34:56 reads 123456");
    check(reads(23, 59, 60, "235960"), "a leap second reads 60");

    struct tm bad = at(24, 0, 0);
    int d[6] = { 7, 7, 7, 7, 7, 7 };
    check(!face_digits(&bad, d) && d[0] == 7, "hour 24 is refused and the digits left alone");
    bad = at(12, 60, 0);
    check(!face_digits(&bad, d), "minute 60 is refused");
    bad = at(12, 0, -1);
    check(!face_digits(&bad, d), "second -1 is refused");
    check(!face_digits(NULL, d), "no time (localtime failed) is refused");
}

static void test_colons(void)
{
    check(face_colons_lit(0), "colons are lit at the top of the second");
    check(face_colons_lit(499999999L), "and to the last nanosecond of the first half");
    check(!face_colons_lit(500000000L), "and unlit from the half-second");
    check(!face_colons_lit(999999999L), "until the next second");
}

static void test_next_change(void)
{
    check(face_ms_to_next_change(0) == 501, "at a change, the next is 500 ms away, plus one");
    check(face_ms_to_next_change(250000000L) == 251, "a quarter-second in, 250 ms, plus one");
    check(face_ms_to_next_change(499999999L) == 2, "a nanosecond short of the half, 1 ms, plus one");
    check(face_ms_to_next_change(500000000L) == 501, "at the half-second, 500 ms, plus one");
    check(face_ms_to_next_change(999000000L) == 2, "a millisecond short of the second, 1 ms, plus one");

    /* Every wait, from anywhere in the second, ends past exactly one change
     * and within 2 ms of it, so no change is skipped and none shown late. */
    const long long half = 500000000LL, ms = 1000000LL;
    int ok = 1;
    for (long nsec = 0; nsec < 1000000000L && ok; nsec += 997) {
        long long end  = nsec + face_ms_to_next_change(nsec) * ms;
        long long next = (nsec / half + 1) * half;
        ok = end >= next && end < next + 2 * ms && end < next + half;
    }
    check(ok, "every wait lands within 2 ms after the next change");
    check(face_ms_to_next_change(-1) >= 2 && face_ms_to_next_change(-1) <= 501,
          "a negative fraction still gives a wait in range");
}

int main(void)
{
    test_segments();
    test_palette();
    test_dim();
    test_digits();
    test_colons();
    test_next_change();
    printf("\n%s: %d of %d checks failed\n", failures ? "FAILED" : "passed",
           failures, checks);
    return failures ? 1 : 0;
}
