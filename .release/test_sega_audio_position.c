#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../source/lindberghrecomp/src/runtime/sega_audio_position.h"
int main(void)
{
    /* Captured in Chapter 1: the original API returned byte 65536 here. */
    double position=sega_loop_position(16384.000000785374,0,16384);
    assert((uint32_t)position*4==0);
    assert(position>0 && position<.000001);
    assert(sega_loop_position(16384,0,16384)==0);
    assert(sega_loop_position(3000.75,512,3072)==3000.75);
    assert(sega_loop_position(3072.5,512,3072)==512.5);
    assert(sega_loop_position(8192.25,512,3072)==512.25);
    assert(sega_loop_position(4000,3072,512)==4000);
    const int rates[]={22050,44100,48000};
    for(unsigned r=0;r<sizeof rates/sizeof *rates;r++){
        position=0;
        for(unsigned callback=1;callback<100000;callback++){
            position=sega_loop_position(position+1024.0*rates[r]/48000,0,16384);
            assert(position>=0 && position<16384);
            assert((uint32_t)position*4<65536);
            double expected=fmod(callback*1024.0*rates[r]/48000,16384);
            double error=fabs(position-expected);if(error>8192)error=16384-error;
            assert(error<.0001);
        }
    }
    puts("PASS captured ring boundary, fractional phase, subloops and 100000 callbacks at each sample rate");
    return 0;
}
