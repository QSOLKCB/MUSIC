#ifndef MUSIC_AUDIO_H
#define MUSIC_AUDIO_H
#include "music.h"
typedef struct MusicAudio MusicAudio;
MusicAudio *audio_new(int null_device);
void audio_free(MusicAudio *audio);
int audio_start(MusicAudio *audio,const MusicProject *project,uint32_t position,int record,char error[256]);
/* Stop/join the device before editing the project. Caller owns returned capture. */
int16_t *audio_stop(MusicAudio *audio,uint32_t *frames,uint32_t *start);
uint32_t audio_position(const MusicAudio *audio);
int audio_finished(const MusicAudio *audio);
int audio_active(const MusicAudio *audio);
int audio_recording(const MusicAudio *audio);
#endif
