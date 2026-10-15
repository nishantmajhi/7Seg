/*
 * face.h — what the clock face shows, decided without drawing anything.
 *
 * Which segments a digit lights, which colour a menu entry means, which
 * digits a time reads as, and whether the colons are lit. clock.c draws.
 */
#ifndef FACE_H
#define FACE_H

#include <time.h>

/* Segment bits, in the conventional a-g order:
 *
 *      a          a  top           e  lower left
 *    f   b        b  upper right   f  upper left
 *      g          c  lower right   g  middle
 *    e   c        d  bottom
 *      d
 */
enum {
    SEG_A = 1 << 0, SEG_B = 1 << 1, SEG_C = 1 << 2, SEG_D = 1 << 3,
    SEG_E = 1 << 4, SEG_F = 1 << 5, SEG_G = 1 << 6
};

/* The segments digit 0-9 lights; 0 for anything else. */
unsigned face_segments(int digit);

typedef struct { const char *name; unsigned char r, g, b; } FaceColor;

#define FACE_N_COLORS      8
#define FACE_DEFAULT_COLOR 6   /* Purple */
extern const FaceColor FACE_PALETTE[FACE_N_COLORS];

/* The palette index of a colour's menu name, or -1. */
int face_color_index(const char *name);

/* An unlit segment's colour is DIM_FACTOR of the lit one, but never darker
 * than DIM_FLOOR. On the white face the floor always wins, so every unlit
 * segment is the same light grey (0.96) whatever the colour; the factor is
 * what an unlit segment would be on a dark face. */
#define FACE_DIM_FACTOR 0.10
#define FACE_DIM_FLOOR  0.96
double face_dim(double channel);

/* The six digits HHMMSS for a broken-down local time. Returns 0, leaving
 * digits alone, if tm is NULL or out of range. */
int face_digits(const struct tm *tm, int digits[6]);

/* The colons are lit for the first half of every second. */
int face_colons_lit(long nsec);

/* What the face shows changes on the second and on the half-second. Given
 * the nanoseconds into the current second, the milliseconds to wait to be
 * just past the next change: rounded up, plus one. Between 2 and 501. */
int face_ms_to_next_change(long nsec);

#endif
