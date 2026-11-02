/* Pinned miniaudio supplies portable devices and atomic operations for C99. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MusicAudio {
    ma_context context;
    ma_device device;
    int context_ready, active, record;
    const MusicProject *project; /* Immutable until ma_device_uninit joins callbacks. */
    int16_t *capture;
    uint32_t start, capacity;
    volatile ma_uint64 cursor;
    volatile ma_uint32 finished;
};
static void callback(ma_device *device,void *output,const void *input,ma_uint32 frames) {
    MusicAudio *a=(MusicAudio*)device->pUserData;
    uint32_t cursor=(uint32_t)ma_atomic_load_64(&a->cursor), n=frames;
    uint32_t end=a->record?a->start+a->capacity:music_duration(a->project);
    if(cursor>=end) n=0; else if(n>end-cursor) n=end-cursor;
    if(output) {
        memset(output,0,(size_t)frames*4);
        if(n) music_render(a->project,cursor,n,(int16_t*)output);
    }
    if(a->record && n) {
        int16_t *destination=a->capture+(size_t)(cursor-a->start)*2;
        if(input) memcpy(destination,input,(size_t)n*4);
        else memset(destination,0,(size_t)n*4);
    }
    ma_atomic_store_64(&a->cursor,cursor+n);
    if(cursor+n>=end) ma_atomic_store_32(&a->finished,1);
}
MusicAudio *audio_new(int null_device) {
    MusicAudio *a=(MusicAudio*)calloc(1,sizeof(*a)); ma_backend backend=ma_backend_null;
    if(!a) return NULL;
    if(ma_context_init(null_device?&backend:NULL,null_device?1:0,NULL,&a->context)==MA_SUCCESS) a->context_ready=1;
    return a;
}
int audio_start(MusicAudio *a,const MusicProject *p,uint32_t start,int record,char error[256]) {
    ma_device_config config; ma_result result; size_t available;
    if(!a || !a->context_ready) { snprintf(error,256,"No local audio backend is available."); return 0; }
    if(a->active || !music_valid(p) || start>=MUSIC_TIMELINE_FRAMES) { snprintf(error,256,"Stop playback before changing audio."); return 0; }
    if(record && p->count>=MUSIC_TRACKS) { snprintf(error,256,"Remove a track before recording; the preview supports 16."); return 0; }
    if(!record && start>=music_duration(p)) { snprintf(error,256,"Move the playhead before the end of the arrangement."); return 0; }
    a->project=p; a->start=start; a->record=record;
    ma_atomic_store_64(&a->cursor,start); ma_atomic_store_32(&a->finished,0);
    if(record) {
        available=(MUSIC_MEMORY_LIMIT-music_memory(p))/4;
        a->capacity=(uint32_t)(available>MUSIC_CLIP_FRAMES?MUSIC_CLIP_FRAMES:available);
        if(a->capacity>MUSIC_TIMELINE_FRAMES-start) a->capacity=MUSIC_TIMELINE_FRAMES-start;
        if(!a->capacity) { snprintf(error,256,"Project audio memory limit reached."); return 0; }
        a->capture=(int16_t*)malloc((size_t)a->capacity*4);
        if(!a->capture) { snprintf(error,256,"Not enough memory for a recording."); return 0; }
    }
    config=ma_device_config_init(record?ma_device_type_duplex:ma_device_type_playback);
    config.sampleRate=MUSIC_RATE;
    config.playback.format=ma_format_s16; config.playback.channels=2;
    if(record) { config.capture.format=ma_format_s16; config.capture.channels=2; }
    config.dataCallback=callback; config.pUserData=a;
    result=ma_device_init(&a->context,&config,&a->device);
    if(result!=MA_SUCCESS) {
        free(a->capture); a->capture=NULL;
        snprintf(error,256,"Cannot open the local audio device: %s",ma_result_description(result)); return 0;
    }
    result=ma_device_start(&a->device);
    if(result!=MA_SUCCESS) {
        ma_device_uninit(&a->device); free(a->capture); a->capture=NULL;
        snprintf(error,256,"Cannot start the audio device: %s",ma_result_description(result)); return 0;
    }
    a->active=1; return 1;
}
int16_t *audio_stop(MusicAudio *a,uint32_t *frames,uint32_t *start) {
    int16_t *data=NULL; *frames=0; *start=0;
    if(a && a->active) {
        ma_device_uninit(&a->device); a->active=0;
        if(a->record) { *frames=(uint32_t)ma_atomic_load_64(&a->cursor)-a->start; *start=a->start; data=a->capture; a->capture=NULL; }
        a->record=0;
    }
    return data;
}
void audio_free(MusicAudio *a) {
    uint32_t frames,start;
    if(!a) return;
    free(audio_stop(a,&frames,&start)); free(a->capture);
    if(a->context_ready) ma_context_uninit(&a->context);
    free(a);
}
uint32_t audio_position(const MusicAudio *a) { return a?(uint32_t)ma_atomic_load_64(&a->cursor):0; }
int audio_finished(const MusicAudio *a) { return a && ma_atomic_load_32(&a->finished)!=0; }
int audio_active(const MusicAudio *a) { return a && a->active; }
int audio_recording(const MusicAudio *a) { return a && a->active && a->record; }
