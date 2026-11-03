#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
/* Track allocation requests, not logical frame counts or allocator-specific RSS. */
typedef struct { void *p; size_t bytes; } Allocation;
static Allocation allocations[32];
static unsigned shrink_calls;
static int fail_shrink;
static void *tracked_malloc(size_t bytes) {
    unsigned i; void *p=malloc(bytes); CHECK(p);
    for(i=0;i<32;++i) if(!allocations[i].p) { allocations[i].p=p;allocations[i].bytes=bytes;return p; }
    CHECK(0);return NULL;
}
static size_t live_bytes(void) { size_t n=0;unsigned i;for(i=0;i<32;++i)n+=allocations[i].bytes;return n; }
static void tracked_free(void *p) {
    unsigned i;if(!p)return;
    for(i=0;i<32;++i) if(allocations[i].p==p) { allocations[i].p=NULL;allocations[i].bytes=0;free(p);return; }
    CHECK(0);
}
static void *tracked_realloc(void *p,size_t bytes);
#define malloc tracked_malloc
#define realloc tracked_realloc
#define free tracked_free
#include "../src/audio.c"
#undef malloc
#undef realloc
#undef free
static void *tracked_realloc(void *p,size_t bytes) {
    unsigned i;void *q;CHECK(device_joined);++shrink_calls;
    if(fail_shrink)return NULL;
    for(i=0;i<32;++i) if(allocations[i].p==p) {
        q=realloc(p,bytes);CHECK(q);allocations[i].p=q;allocations[i].bytes=bytes;return q;
    }
    CHECK(0);return NULL;
}
static void release_project(MusicProject *p) {
    unsigned i;for(i=0;i<p->count;++i)tracked_free(p->tracks[i].samples);music_init(p);CHECK(!live_bytes());
}
static int16_t *stop_at(MusicAudio *a,uint64_t cursor,uint32_t *n,uint32_t *start,uint32_t *cap) {
    a->cursor=cursor;return audio_stop(a,n,start,cap);
}
static void repeated_short_takes(void) {
    MusicProject p;MusicAudio a={0};unsigned i;char error[256];uint32_t n,start,cap;int16_t *take;
    music_init(&p);a.context_ready=1;
    for(i=0;i<MUSIC_TRACKS;++i) {
        CHECK(audio_start(&a,&p,123,1,error));CHECK(a.capacity==MUSIC_CLIP_FRAMES);
        a.capture[0]=1234;a.capture[MUSIC_RATE*2-1]=-1234;
        take=stop_at(&a,123+MUSIC_RATE,&n,&start,&cap);
        CHECK(device_joined && !a.capture && !a.active && !a.record);
        CHECK(n==MUSIC_RATE && cap==n && start==123);
        CHECK(take[0]==1234 && take[n*2-1]==-1234);
        CHECK(music_add_pcm_capacity(&p,"short",take,n,cap,start,error));
        CHECK(live_bytes()==(size_t)(i+1)*MUSIC_RATE*4);
        CHECK(live_bytes()==music_memory(&p) && music_valid(&p));
    }
    CHECK(!audio_start(&a,&p,0,1,error));release_project(&p);
}
static void empty_full_and_bounds(void) {
    MusicProject p;MusicAudio a={0};char error[256];uint32_t n,start,cap;int16_t *take;unsigned before;
    uint64_t invalid[]={0,UINT64_MAX,(uint64_t)UINT32_MAX+11};unsigned i;
    music_init(&p);a.context_ready=1;
    CHECK(audio_start(&a,&p,10,1,error));
    CHECK(!stop_at(&a,10,&n,&start,&cap));CHECK(!n && !cap && !live_bytes());
    for(i=0;i<3;++i) {
        CHECK(audio_start(&a,&p,10,1,error));
        CHECK(!stop_at(&a,invalid[i],&n,&start,&cap));CHECK(!n && !cap && !live_bytes());
    }
    CHECK(audio_start(&a,&p,10,1,error));before=shrink_calls;
    take=stop_at(&a,(uint64_t)10+a.capacity,&n,&start,&cap);
    CHECK(n==MUSIC_CLIP_FRAMES && n==cap && before==shrink_calls);
    CHECK(live_bytes()==(size_t)cap*4);tracked_free(take);
    CHECK(!audio_stop(&a,&n,&start,&cap) && !n && !cap);
}
static void shrink_failure_budget(void) {
    MusicProject p;MusicAudio a={0};char error[256];uint32_t n,start,cap;int16_t *take,*owned;unsigned i;
    music_init(&p);a.context_ready=1;fail_shrink=1;
    for(i=0;i<3;++i) {
        size_t available=MUSIC_MEMORY_LIMIT-music_memory(&p);
        CHECK(audio_start(&a,&p,0,1,error));
        CHECK((size_t)a.capacity*4==(available>(size_t)MUSIC_CLIP_FRAMES*4?(size_t)MUSIC_CLIP_FRAMES*4:available));
        a.capture[0]=777;take=stop_at(&a,1,&n,&start,&cap);
        CHECK(take && take[0]==777 && n==1 && cap>n);
        CHECK(music_add_pcm_capacity(&p,"OOM retained",take,n,cap,start,error));
        CHECK(music_valid(&p) && music_memory(&p)==live_bytes());
    }
    CHECK(live_bytes()==MUSIC_MEMORY_LIMIT);CHECK(!audio_start(&a,&p,0,1,error));
    owned=tracked_malloc(4);
    CHECK(!music_add_pcm_capacity(&p,"over budget",owned,1,1,0,error));tracked_free(owned);
    p.tracks[0].capacity=0;CHECK(!music_valid(&p));p.tracks[0].capacity=MUSIC_CLIP_FRAMES;
    release_project(&p);fail_shrink=0;
}
int main(void) {
    repeated_short_takes();empty_full_and_bounds();shrink_failure_budget();
    CHECK(!live_bytes());puts("PASS: recording capacity, join, contents, bounds, OOM ownership and budget");return 0;
}
