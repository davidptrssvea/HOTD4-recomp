#ifndef SEGA_AUDIO_POSITION_H
#define SEGA_AUDIO_POSITION_H
#include <math.h>
#include <stdint.h>
/* A circular play cursor is half-open: [start,end), including the instant
 * between callbacks. Retain its fractional sample when crossing a boundary. */
static inline double sega_loop_position(double position,uint32_t start,uint32_t end)
{
    if(end>start && position>=end)
        return start+fmod(position-start,(double)(end-start));
    return position;
}
#endif
