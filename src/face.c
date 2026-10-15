/*
 * face.c — what the clock face shows, decided without drawing anything.
 */
#include <stddef.h>
#include <string.h>

#include "face.h"

static const unsigned char DIGIT_SEGMENTS[10] = {
    0b0111111,  /* 0 */
    0b0000110,  /* 1 */
    0b1011011,  /* 2 */
    0b1001111,  /* 3 */
    0b1100110,  /* 4 */
    0b1101101,  /* 5 */
    0b1111101,  /* 6 */
    0b0000111,  /* 7 */
    0b1111111,  /* 8 */
    0b1101111   /* 9 */
};

unsigned face_segments(int digit)
{
    if (digit < 0 || digit > 9) return 0;
    return DIGIT_SEGMENTS[digit];
}

const FaceColor FACE_PALETTE[FACE_N_COLORS] = {
    { "Lime",   0x16, 0xC6, 0x0C },
    { "Amber",  0xFF, 0x8C, 0x00 },
    { "Red",    0xE8, 0x20, 0x20 },
    { "Teal",   0x00, 0x8B, 0xA8 },
    { "Navy",   0x18, 0x4C, 0xD0 },
    { "Pink",   0xFF, 0x30, 0x80 },
    { "Purple", 0xA0, 0x40, 0xF0 },   /* FACE_DEFAULT_COLOR */
    { "Gold",   0xC8, 0x9A, 0x00 }
};

int face_color_index(const char *name)
{
    if (name == NULL) return -1;
    for (int i = 0; i < FACE_N_COLORS; i++)
        if (strcmp(FACE_PALETTE[i].name, name) == 0) return i;
    return -1;
}

double face_dim(double channel)
{
    double dimmed = channel * FACE_DIM_FACTOR;
    return FACE_DIM_FLOOR > dimmed ? FACE_DIM_FLOOR : dimmed;
}

int face_digits(const struct tm *tm, int digits[6])
{
    if (tm == NULL ||
        tm->tm_hour < 0 || tm->tm_hour > 23 ||
        tm->tm_min  < 0 || tm->tm_min  > 59 ||
        tm->tm_sec  < 0 || tm->tm_sec  > 60)      /* 60: a leap second */
        return 0;
    digits[0] = tm->tm_hour / 10;  digits[1] = tm->tm_hour % 10;
    digits[2] = tm->tm_min  / 10;  digits[3] = tm->tm_min  % 10;
    digits[4] = tm->tm_sec  / 10;  digits[5] = tm->tm_sec  % 10;
    return 1;
}

int face_colons_lit(long nsec)
{
    return nsec < 500000000L;
}

int face_ms_to_next_change(long nsec)
{
    const long half = 500000000L, ms = 1000000L;
    long into = nsec % half;
    if (into < 0) into += half;
    return (int)((half - into + ms - 1) / ms) + 1;
}
