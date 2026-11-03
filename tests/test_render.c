#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
void render_reference(const MusicProject *,uint32_t,uint32_t,int16_t *);
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static uint32_t state=0x51a7u;
static uint32_t random_word(void) { state=state*1664525u+1013904223u; return state; }
static void fixture(MusicProject *p,unsigned count) {
    unsigned t,j; char error[256]; music_init(p);
    for(t=0;t<count;++t) {
        int16_t *s=malloc(4096*4); CHECK(s);
        for(j=0;j<8192;++j) s[j]=(int16_t)((int)(random_word()%65536u)-32768);
        CHECK(music_add_pcm(p,"synthetic",s,4096,0,error));
    }
}
static void compare(const MusicProject *p,uint32_t start,uint32_t frames) {
    int16_t a[8194],b[8194];
    CHECK(frames<=4096); memset(a,0x55,sizeof(a)); memcpy(b,a,sizeof(a));
    music_render(p,start,frames,a+1); render_reference(p,start,frames,b+1);
    CHECK(!memcmp(a,b,sizeof(a)));
}
static void conformance(void) {
    MusicProject p; unsigned r,t,j;
    fixture(&p,16);
    for(r=0;r<2000;++r) {
        p.count=random_word()%17;
        for(t=0;t<16;++t) {
            MusicTrackData *v=&p.tracks[t];
            v->offset=random_word()%512; v->trim_in=random_word()%4096;
            v->trim_out=v->trim_in+1+random_word()%(4096-v->trim_in);
            v->gain=random_word()%(4*MUSIC_UNITY+1); v->mute=(random_word()%7)==0; v->solo=(random_word()%11)==0;
        }
        compare(&p,random_word()%4608,random_word()%4097);
        compare(&p,UINT32_MAX-2,16); compare(&p,0,0);
    }
    p.count=16;
    for(t=0;t<16;++t) {
        MusicTrackData *v=&p.tracks[t]; v->offset=0; v->trim_in=0; v->trim_out=4096;
        v->mute=v->solo=0; v->gain=4*MUSIC_UNITY;
        for(j=0;j<8192;++j) v->samples[j]=(j%4<2)?-32768:32767;
    }
    compare(&p,0,4096); /* Full positive/negative sums, saturation. */
    for(t=8;t<16;++t) for(j=0;j<8192;++j) p.tracks[t].samples[j]=(int16_t)(-p.tracks[t].samples[j]-1);
    compare(&p,0,4096); /* Cancellation must precede rounding/clipping. */
    for(t=0;t<16;++t) {
        p.tracks[t].offset=t*3; p.tracks[t].trim_in=4080+t%15;
        p.tracks[t].gain=t*12345;
    }
    for(j=0;j<80;++j) compare(&p,j,65); /* Seek, short spans, gain ramp across tracks. */
    p.tracks[0].solo=p.tracks[0].mute=1; compare(&p,0,4096);
    /* Every call observes newly edited controls; split calls equal one call. */
    p.tracks[0].mute=p.tracks[0].solo=0;
    for(j=0;j<16;++j) {
        int16_t whole[514],parts[514];
        p.tracks[j].gain=j*17321; p.tracks[j].mute=j%2; p.tracks[j].solo=(j==7);
        compare(&p,0,257);
        music_render(&p,0,257,whole); music_render(&p,0,33,parts); music_render(&p,33,224,parts+66);
        CHECK(!memcmp(whole,parts,sizeof(whole)));
    }
    /* Preserve original frame-order behavior even for an overlapping output. */
    { int16_t saved[8192],expected[8192];
      p.count=1; p.tracks[0].mute=p.tracks[0].solo=0; p.tracks[0].trim_in=0; p.tracks[0].trim_out=4096;
      memcpy(saved,p.tracks[0].samples,sizeof(saved));
      render_reference(&p,0,100,p.tracks[0].samples+2); memcpy(expected,p.tracks[0].samples,sizeof(expected));
      memcpy(p.tracks[0].samples,saved,sizeof(saved)); music_render(&p,0,100,p.tracks[0].samples+2);
      CHECK(!memcmp(expected,p.tracks[0].samples,sizeof(expected))); p.count=16;
    }
    music_free(&p); puts("render differential conformance passed (2000 seeded projects + boundary cases)");
}
static volatile unsigned checksum;
static double measure(void (*render)(const MusicProject *,uint32_t,uint32_t,int16_t *),const MusicProject *p,unsigned frames) {
    unsigned j; int16_t out[2048]; clock_t begin=clock();
    for(j=0;j<12000;++j) { render(p,(j%3)*128,frames,out); checksum+=(unsigned)(uint16_t)out[j%(frames*2)]; }
    return (double)(clock()-begin)/CLOCKS_PER_SEC;
}
int main(int argc,char **argv) {
    conformance();
    if(argc>1 && !strcmp(argv[1],"--benchmark")) {
        unsigned c,r,n,mode,t; MusicProject p; const unsigned counts[]={0,1,8,16}; const unsigned sizes[]={64,256,1024};
        puts("mode,tracks,frames,repeat,reference_seconds,optimized_seconds");
        for(mode=0;mode<3;++mode) for(c=0;c<4;++c) { fixture(&p,counts[c]);
            for(t=0;t<p.count;++t) {
                if(mode==1) p.tracks[t].mute=(t%4)!=0;
                if(mode==2) { p.tracks[t].offset=t*32; p.tracks[t].trim_out=64+t*8; }
            }
            for(n=0;n<3;++n) for(r=0;r<7;++r) {
                double a,b;
                if(r%2) { b=measure(music_render,&p,sizes[n]); a=measure(render_reference,&p,sizes[n]); }
                else { a=measure(render_reference,&p,sizes[n]); b=measure(music_render,&p,sizes[n]); }
                printf("%u,%u,%u,%u,%.9f,%.9f\n",mode,counts[c],sizes[n],r,a,b);
            }
            music_free(&p);
        }
    }
    return 0;
}
