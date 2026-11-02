/* Portable C99 audio model. No GUI, device, filesystem or network dependency. */
#include "music.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int fail(char error[256], const char *message) {
    snprintf(error, 256, "%s", message); return 0;
}
static uint16_t u16(const unsigned char *p) { return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8)); }
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int16_t s16(const unsigned char *p) { unsigned v = u16(p); return (int16_t)(v >= 32768u ? (int)v - 65536 : (int)v); }
static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
void music_init(MusicProject *p) { memset(p, 0, sizeof(*p)); p->bpm=120; }
void music_free(MusicProject *p) {
    unsigned i; for (i=0; i<p->count; ++i) free(p->tracks[i].samples); music_init(p);
}
void music_remove(MusicProject *p, unsigned i) {
    if (i>=p->count) return;
    free(p->tracks[i].samples);
    memmove(&p->tracks[i], &p->tracks[i+1], (p->count-i-1)*sizeof(MusicTrack));
    memset(&p->tracks[--p->count], 0, sizeof(MusicTrack));
}
size_t music_memory(const MusicProject *p) {
    size_t total=0; unsigned i;
    for (i=0;i<p->count;++i) total += (size_t)p->tracks[i].frames * 4;
    return total;
}
int music_valid(const MusicProject *p) {
    unsigned i;
    if (p->count > MUSIC_TRACKS || p->bpm<40 || p->bpm>240) return 0;
    for (i=0;i<p->count;++i) {
        const MusicTrack *t=&p->tracks[i];
        if (!t->samples || !t->frames || t->frames>MUSIC_CLIP_FRAMES ||
            t->trim_in>=t->trim_out || t->trim_out>t->frames ||
            t->offset>MUSIC_TIMELINE_FRAMES-(t->trim_out-t->trim_in) ||
            t->gain>4*MUSIC_UNITY || t->mute>1 || t->solo>1 ||
            !memchr(t->name,0,sizeof(t->name))) return 0;
    }
    return music_memory(p)<=MUSIC_MEMORY_LIMIT;
}
uint32_t music_duration(const MusicProject *p) {
    uint32_t end=0; unsigned i;
    for (i=0;i<p->count;++i) {
        const MusicTrack *t=&p->tracks[i]; uint32_t n=t->offset+t->trim_out-t->trim_in;
        if (n>end) end=n;
    }
    return end;
}
int music_add_pcm(MusicProject *p,const char *name,int16_t *samples,uint32_t frames,uint32_t offset,char error[256]) {
    MusicTrack *t;
    if (!samples || !frames || frames>MUSIC_CLIP_FRAMES) return fail(error,"Clip must contain 1 frame to 10 minutes of audio.");
    if (p->count>=MUSIC_TRACKS) return fail(error,"This preview supports 16 tracks.");
    if (offset>MUSIC_TIMELINE_FRAMES-frames) return fail(error,"Arrangement exceeds the 30-minute preview limit.");
    if ((size_t)frames*4 > MUSIC_MEMORY_LIMIT-music_memory(p)) return fail(error,"Project audio exceeds the 256 MiB preview limit.");
    t=&p->tracks[p->count++]; memset(t,0,sizeof(*t));
    snprintf(t->name,sizeof(t->name),"%s",name); t->samples=samples;
    t->frames=t->trim_out=frames; t->offset=offset; t->gain=MUSIC_UNITY;
    return 1;
}

int music_import_wav(MusicProject *p,const char *name,const unsigned char *data,size_t size,char error[256]) {
    size_t at=12,limit,pcm_size=0; const unsigned char *pcm=NULL;
    unsigned channels=0,rate=0,bits=0,format=0,align=0; int have_fmt=0;
    uint32_t source_frames,frames,i; int16_t *samples;
    if (size<12 || memcmp(data,"RIFF",4) || memcmp(data+8,"WAVE",4)) return fail(error,"Choose a PCM WAV file.");
    if ((uint64_t)u32(data+4)+8>size || u32(data+4)<4) return fail(error,"Truncated WAV container.");
    limit=(size_t)u32(data+4)+8;
    while (at<limit) {
        uint32_t n; size_t next;
        if (limit-at<8) return fail(error,"Truncated WAV chunk header.");
        n=u32(data+at+4); at+=8;
        if ((uint64_t)n>limit-at) return fail(error,"Truncated WAV chunk.");
        if (!memcmp(data+at-8,"fmt ",4)) {
            if (have_fmt || n<16) return fail(error,"Invalid or repeated WAV format chunk.");
            format=u16(data+at); channels=u16(data+at+2); rate=u32(data+at+4);
            align=u16(data+at+12); bits=u16(data+at+14); have_fmt=1;
            if (format!=1 || (channels!=1 && channels!=2) || bits!=16 ||
                (rate!=44100 && rate!=48000) || align!=channels*2 || u32(data+at+8)!=rate*align)
                return fail(error,"Use 16-bit PCM WAV, mono/stereo, 44.1 or 48 kHz.");
        } else if (!memcmp(data+at-8,"data",4)) {
            if (pcm) return fail(error,"Repeated WAV audio chunk.");
            pcm=data+at; pcm_size=n;
        }
        next=at+(size_t)n;
        if (n&1u) { if (next>=limit) return fail(error,"Missing WAV padding byte."); ++next; }
        at=next;
    }
    if (!have_fmt || !pcm || !pcm_size || pcm_size%align) return fail(error,"WAV contains no complete audio frames.");
    source_frames=(uint32_t)(pcm_size/align);
    if ((uint64_t)source_frames*MUSIC_RATE/rate>MUSIC_CLIP_FRAMES) return fail(error,"Clip exceeds the 10-minute preview limit.");
    frames=(uint32_t)((uint64_t)source_frames*MUSIC_RATE/rate);
    if (p->count>=MUSIC_TRACKS || (size_t)frames*4>MUSIC_MEMORY_LIMIT-music_memory(p)) return fail(error,"Project track or memory limit reached.");
    samples=(int16_t*)malloc((size_t)frames*4);
    if (!samples) return fail(error,"Not enough memory for this audio.");
    for (i=0;i<frames;++i) {
        uint64_t position=(uint64_t)i*rate; uint32_t source=(uint32_t)(position/MUSIC_RATE);
        uint32_t fraction=(uint32_t)(position%MUSIC_RATE), next=source+1<source_frames?source+1:source;
        unsigned channel;
        for (channel=0;channel<2;++channel) {
            unsigned c=channels==1?0:channel;
            int a=s16(pcm+(size_t)source*align+c*2), b=s16(pcm+(size_t)next*align+c*2);
            samples[(size_t)i*2+channel]=(int16_t)(a+((int64_t)(b-a)*fraction)/MUSIC_RATE);
        }
    }
    if (!music_add_pcm(p,name,samples,frames,0,error)) { free(samples); return 0; }
    return 1;
}

void music_render(const MusicProject *p,uint32_t start,uint32_t frames,int16_t *out) {
    uint32_t f; unsigned i,any_solo=0;
    for (i=0;i<p->count;++i) if(p->tracks[i].solo) any_solo=1;
    for(f=0;f<frames;++f) {
        int64_t left=0,right=0; uint64_t position=(uint64_t)start+f;
        for(i=0;i<p->count;++i) {
            const MusicTrack *t=&p->tracks[i]; uint64_t index;
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
static int write_pcm(MusicWrite write,void *ctx,const int16_t *samples,uint32_t frames) {
    unsigned char bytes[4096]; uint32_t pos=0;
    while(pos<frames) {
        uint32_t n=frames-pos>1024?1024:frames-pos,i;
        for(i=0;i<n*2;++i) put16(bytes+i*2,(uint16_t)samples[(size_t)pos*2+i]);
        if(!write(ctx,bytes,(size_t)n*4)) return 0;
        pos+=n;
    }
    return 1;
}
int music_export_wav(const MusicProject *p,MusicWrite write,void *ctx,char error[256]) {
    unsigned char header[44]={0}; uint32_t duration,pos=0; int16_t block[2048];
    if(!music_valid(p)) return fail(error,"Project settings are invalid.");
    duration=music_duration(p);
    if(!duration) return fail(error,"Add audio before exporting.");
    memcpy(header,"RIFF",4); put32(header+4,36+duration*4); memcpy(header+8,"WAVEfmt ",8);
    put32(header+16,16); put16(header+20,1); put16(header+22,2); put32(header+24,MUSIC_RATE);
    put32(header+28,MUSIC_RATE*4); put16(header+32,4); put16(header+34,16);
    memcpy(header+36,"data",4); put32(header+40,duration*4);
    if(!write(ctx,header,sizeof(header))) return fail(error,"Could not write WAV header.");
    while(pos<duration) {
        uint32_t n=duration-pos>1024?1024:duration-pos;
        music_render(p,pos,n,block);
        if(!write_pcm(write,ctx,block,n)) return fail(error,"Could not write WAV audio.");
        pos+=n;
    }
    return 1;
}
int music_save(const MusicProject *p,MusicWrite write,void *ctx,char error[256]) {
    unsigned char header[20]={0}; unsigned i;
    if(!music_valid(p)) return fail(error,"Project settings are invalid.");
    memcpy(header,"MUSICP01",8); put32(header+8,MUSIC_RATE); put32(header+12,p->count); put32(header+16,p->bpm);
    if(!write(ctx,header,20)) return fail(error,"Could not write project header.");
    for(i=0;i<p->count;++i) {
        const MusicTrack *t=&p->tracks[i]; unsigned char meta[28]; size_t name_len=strlen(t->name);
        put32(meta,(uint32_t)name_len); put32(meta+4,t->frames); put32(meta+8,t->offset);
        put32(meta+12,t->trim_in); put32(meta+16,t->trim_out); put32(meta+20,t->gain);
        put32(meta+24,t->mute|(t->solo<<1));
        if(!write(ctx,meta,28) || !write(ctx,t->name,name_len) || !write_pcm(write,ctx,t->samples,t->frames))
            return fail(error,"Could not finish saving the project.");
    }
    return 1;
}
int music_load(MusicProject *destination,const unsigned char *data,size_t size,char error[256]) {
    MusicProject loaded; uint32_t count,i; size_t at=20; music_init(&loaded);
    if(size<20 || memcmp(data,"MUSICP01",8) || u32(data+8)!=MUSIC_RATE || u32(data+12)>MUSIC_TRACKS || u32(data+16)<40 || u32(data+16)>240)
        return fail(error,"Not a supported MUSIC project.");
    count=u32(data+12); loaded.bpm=u32(data+16);
    for(i=0;i<count;++i) {
        uint32_t length,frames,offset,in,out,gain,flags,j; int16_t *samples; char name[128];
        if(size-at<28) goto invalid;
        length=u32(data+at); frames=u32(data+at+4); offset=u32(data+at+8);
        in=u32(data+at+12); out=u32(data+at+16); gain=u32(data+at+20); flags=u32(data+at+24); at+=28;
        if(length>=sizeof(name) || length>size-at || !frames || frames>MUSIC_CLIP_FRAMES ||
           (uint64_t)frames*4>size-at-length || in>=out || out>frames ||
           offset>MUSIC_TIMELINE_FRAMES-(out-in) || gain>4*MUSIC_UNITY || flags>3 ||
           (size_t)frames*4>MUSIC_MEMORY_LIMIT-music_memory(&loaded) || memchr(data+at,0,length)) goto invalid;
        memcpy(name,data+at,length); name[length]=0; at+=length;
        samples=(int16_t*)malloc((size_t)frames*4);
        if(!samples) { music_free(&loaded); return fail(error,"Not enough memory to open this project."); }
        for(j=0;j<frames*2;++j) samples[j]=s16(data+at+(size_t)j*2);
        at+=(size_t)frames*4;
        /* Add full audio at zero first: the saved trim may shorten its placement. */
        if(!music_add_pcm(&loaded,name,samples,frames,0,error)) { free(samples); music_free(&loaded); return 0; }
        loaded.tracks[i].offset=offset; loaded.tracks[i].trim_in=in; loaded.tracks[i].trim_out=out;
        loaded.tracks[i].gain=gain; loaded.tracks[i].mute=flags&1u; loaded.tracks[i].solo=(flags>>1)&1u;
    }
    if(at!=size || !music_valid(&loaded)) goto invalid;
    music_free(destination); *destination=loaded; return 1;
invalid:
    music_free(&loaded); return fail(error,"Damaged or excessive project data; the current project was kept.");
}
int music_demo(MusicProject *p,char error[256]) {
    unsigned track; MusicProject demo; music_init(&demo);
    for(track=0;track<3;++track) {
        uint32_t frames=MUSIC_RATE*8,i; int16_t *samples=(int16_t*)calloc((size_t)frames*2,sizeof(int16_t));
        if(!samples) { music_free(&demo); return fail(error,"Not enough memory for the demo."); }
        for(i=0;i<frames;++i) {
            uint32_t beat=i%(MUSIC_RATE/2),period=track==0?218:track==1?109:73;
            int value=((int)(i%period)*2-(int)period)* (track==0?12000:6500)/(int)period;
            if(track==2) value=value*(int)(MUSIC_RATE/2-beat)/(int)(MUSIC_RATE/2);
            samples[(size_t)i*2]=samples[(size_t)i*2+1]=(int16_t)value;
        }
        if(!music_add_pcm(&demo,track==0?"Bass sketch":track==1?"Lead sketch":"Pulse sketch",samples,frames,track*MUSIC_RATE,error)) {
            free(samples); music_free(&demo); return 0;
        }
        demo.tracks[track].gain=MUSIC_UNITY/3;
    }
    music_free(p); *p=demo; return 1;
}
