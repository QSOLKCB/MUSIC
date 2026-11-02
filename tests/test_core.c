#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
typedef struct { unsigned char *data; size_t size,limit; } Buffer;
static unsigned passed=0;
static int collect(void *context,const void *data,size_t size) {
    Buffer *b=context; unsigned char *next;
    if(size>b->limit-b->size)return 0;
    next=(unsigned char*)realloc(b->data,b->size+size+1);if(!next)return 0;b->data=next;
    memcpy(b->data+b->size,data,size);b->size+=size;return 1;
}
static Buffer buffer(void) {Buffer b={NULL,0,16u*1024u*1024u};return b;}
static void put16(unsigned char *p,unsigned v) {p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,uint32_t v) {p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static void add(MusicProject *p,const int16_t *data,uint32_t frames,uint32_t offset) {
    char error[256];int16_t *copy=(int16_t*)malloc((size_t)frames*4);CHECK(copy);memcpy(copy,data,(size_t)frames*4);CHECK(music_add_pcm(p,"fixture",copy,frames,offset,error));
}
static void mixing(void) {
    MusicProject p;int16_t data[]={1000,-1000,2000,-2000,3000,-3000},other[]={32000,32000,32000,32000};int16_t out[12];
    music_init(&p);add(&p,data,3,1);music_render(&p,0,6,out);
    CHECK(out[0]==0 && out[2]==1000 && out[7]==-3000 && out[8]==0);
    p.tracks[0].trim_in=1;p.tracks[0].gain=MUSIC_UNITY/2;music_render(&p,1,3,out);
    CHECK(out[0]==1000 && out[2]==1500 && out[4]==0);
    add(&p,other,2,1);music_render(&p,1,2,out);CHECK(out[0]==32767 && out[1]==31000);
    p.tracks[0].solo=1;music_render(&p,1,2,out);CHECK(out[0]==1000);
    p.tracks[0].mute=1;music_render(&p,1,2,out);CHECK(out[0]==0); /* Muted solo does not un-solo the rest. */
    p.tracks[0].solo=0;music_render(&p,1,2,out);CHECK(out[0]==32000);
    music_remove(&p,0);CHECK(p.count==1 && p.tracks[0].samples[0]==32000);music_free(&p);++passed;
}
static void negative_clipping(void) {
    MusicProject p;int16_t data[]={-32768,32767},out[2];music_init(&p);add(&p,data,1,0);p.tracks[0].gain=4*MUSIC_UNITY;
    music_render(&p,0,1,out);CHECK(out[0]==-32768 && out[1]==32767);music_free(&p);++passed;
}
static void project_roundtrip(void) {
    MusicProject p,loaded;char error[256];Buffer a=buffer(),b=buffer();int16_t values[]={-32768,32767,1,-1,100,200};
    music_init(&p);music_init(&loaded);add(&p,values,3,17);p.bpm=137;p.tracks[0].trim_in=1;p.tracks[0].gain=12345;p.tracks[0].solo=1;
    CHECK(music_save(&p,collect,&a,error));CHECK(music_load(&loaded,a.data,a.size,error));
    CHECK(loaded.count==1 && loaded.bpm==137 && loaded.tracks[0].offset==17 && loaded.tracks[0].gain==12345);
    CHECK(!memcmp(values,loaded.tracks[0].samples,sizeof(values)));CHECK(music_save(&loaded,collect,&b,error));
    CHECK(a.size==b.size && !memcmp(a.data,b.data,a.size));free(a.data);free(b.data);music_free(&p);music_free(&loaded);++passed;
}
static void rejection_preserves_project(void) {
    MusicProject p;char error[256];Buffer b=buffer();int16_t data[]={11,22};size_t i;music_init(&p);add(&p,data,1,0);CHECK(music_save(&p,collect,&b,error));
    for(i=0;i<b.size;++i) {CHECK(!music_load(&p,b.data,i,error));CHECK(p.count==1 && p.tracks[0].samples[0]==11);}
    put32(b.data+24,UINT32_MAX);CHECK(!music_load(&p,b.data,b.size,error));CHECK(p.tracks[0].samples[1]==22);
    free(b.data);music_free(&p);++passed;
}
static void wav_roundtrip_and_invalid(void) {
    MusicProject p,q;char error[256];Buffer b=buffer();int16_t data[]={-32768,32767,1234,-1234,2,3};unsigned char *bad;size_t i;
    music_init(&p);music_init(&q);add(&p,data,3,0);CHECK(music_export_wav(&p,collect,&b,error));CHECK(b.size==56);
    CHECK(music_import_wav(&q,"wave",b.data,b.size,error));CHECK(q.tracks[0].frames==3 && !memcmp(data,q.tracks[0].samples,sizeof(data)));music_free(&q);
    for(i=0;i<b.size;++i) { CHECK(!music_import_wav(&q,"short",b.data,i,error)); }
    CHECK(q.count==0);
    bad=(unsigned char*)malloc(b.size);CHECK(bad);memcpy(bad,b.data,b.size);put16(bad+34,24);CHECK(!music_import_wav(&q,"bits",bad,b.size,error));
    memcpy(bad,b.data,b.size);put32(bad+40,UINT32_MAX);CHECK(!music_import_wav(&q,"size",bad,b.size,error));
    free(bad);free(b.data);music_free(&p);music_free(&q);++passed;
}
static void mono_resampling(void) {
    unsigned char wav[52]={0};MusicProject p;char error[256];music_init(&p);
    memcpy(wav,"RIFF",4);put32(wav+4,44);memcpy(wav+8,"WAVEfmt ",8);put32(wav+16,16);put16(wav+20,1);put16(wav+22,1);
    put32(wav+24,44100);put32(wav+28,88200);put16(wav+32,2);put16(wav+34,16);memcpy(wav+36,"data",4);put32(wav+40,8);
    put16(wav+44,100);put16(wav+46,200);put16(wav+48,300);put16(wav+50,400);
    CHECK(music_import_wav(&p,"mono",wav,sizeof(wav),error));CHECK(p.tracks[0].frames==4);
    CHECK(p.tracks[0].samples[0]==100 && p.tracks[0].samples[1]==100 && p.tracks[0].samples[2]==191 && p.tracks[0].samples[3]==191);
    music_free(&p);++passed;
}
static void write_failure(void) {
    MusicProject p;char error[256];Buffer b=buffer();int16_t data[]={1,2};music_init(&p);add(&p,data,1,0);b.limit=44;
    CHECK(!music_export_wav(&p,collect,&b,error));free(b.data);b=buffer();b.limit=20;CHECK(!music_save(&p,collect,&b,error));
    free(b.data);music_free(&p);++passed;
}
static void limits(void) {
    MusicProject p;char error[256];int16_t data[]={1,2},*owned;unsigned i;music_init(&p);
    for(i=0;i<MUSIC_TRACKS;++i) { add(&p,data,1,0); }
    owned=(int16_t*)malloc(4);CHECK(owned);
    CHECK(!music_add_pcm(&p,"too many",owned,1,0,error));free(owned);CHECK(p.count==MUSIC_TRACKS);
    p.tracks[0].trim_out=0;CHECK(!music_valid(&p));p.tracks[0].trim_out=1;p.tracks[0].offset=MUSIC_TIMELINE_FRAMES;CHECK(!music_valid(&p));
    p.tracks[0].offset=0;p.bpm=0;CHECK(!music_valid(&p));music_free(&p);++passed;
}
static int file_write(void *f,const void *data,size_t size){return fwrite(data,1,size,(FILE*)f)==size;}
static void demo_export(void) {
    MusicProject p;char error[256];FILE *file;music_init(&p);CHECK(music_demo(&p,error));CHECK(p.count==3 && music_duration(&p)==MUSIC_RATE*10);
    file=fopen("demo-reference.wav","wb");CHECK(file);CHECK(music_export_wav(&p,file_write,file,error));CHECK(!fclose(file));music_free(&p);++passed;
}
int main(void) {
    mixing();negative_clipping();project_roundtrip();rejection_preserves_project();wav_roundtrip_and_invalid();mono_resampling();write_failure();limits();demo_export();
    printf("PASS: %u core test groups (mixing, clipping, projects, malformed input, WAV, resampling, write failure, limits, deterministic render).\n",passed);return 0;
}
