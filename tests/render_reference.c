/* Frozen scalar oracle from MUSIC 25228d4f943862c5dde526e381a26703f60cde4e. */
#include "music.h"
void render_reference(const MusicProject *p,uint32_t start,uint32_t frames,int16_t *out) {
    uint32_t f; unsigned i,any_solo=0;
    for (i=0;i<p->count;++i) if(p->tracks[i].solo) any_solo=1;
    for(f=0;f<frames;++f) {
        int64_t left=0,right=0; uint64_t position=(uint64_t)start+f;
        for(i=0;i<p->count;++i) {
            const MusicTrackData *t=&p->tracks[i]; uint64_t index;
            if(t->mute || (any_solo && !t->solo) || position<t->offset) continue;
            index=position-t->offset+t->trim_in;
            if(index>=t->trim_out) continue;
            left+=(int64_t)t->samples[index*2]*t->gain;
            right+=(int64_t)t->samples[index*2+1]*t->gain;
        }
        left/=MUSIC_UNITY; right/=MUSIC_UNITY;
        out[(size_t)f*2]=(int16_t)(left>32767?32767:left< -32768? -32768:left);
        out[(size_t)f*2+1]=(int16_t)(right>32767?32767:right< -32768? -32768:right);
    }
}
