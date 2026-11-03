#ifndef MUSIC_H
#define MUSIC_H
#include <stddef.h>
#include <stdint.h>

#define MUSIC_VERSION "0.1.0-preview"
#define MUSIC_RATE 48000u
#define MUSIC_TRACKS 16u
#define MUSIC_CLIP_FRAMES (MUSIC_RATE * 600u)
#define MUSIC_TIMELINE_FRAMES (MUSIC_RATE * 1800u)
#define MUSIC_MEMORY_LIMIT (256u * 1024u * 1024u)
#define MUSIC_UNITY 65536u

typedef struct {
    char name[128];
    int16_t *samples; /* Owned, interleaved stereo at 48 kHz. */
    uint32_t frames, capacity, offset, trim_in, trim_out, gain; /* capacity includes retained spare PCM. */
    unsigned mute, solo;
} MusicTrackData;
typedef struct { MusicTrackData tracks[MUSIC_TRACKS]; unsigned count, bpm; } MusicProject;
typedef int (*MusicWrite)(void *context, const void *data, size_t size);

void music_init(MusicProject *project);
void music_free(MusicProject *project);
void music_remove(MusicProject *project, unsigned track);
size_t music_memory(const MusicProject *project);
int music_valid(const MusicProject *project);
uint32_t music_duration(const MusicProject *project);
int music_add_pcm(MusicProject *project, const char *name, int16_t *owned_stereo,
                  uint32_t frames, uint32_t offset, char error[256]);
/* Takes ownership only on success; capacity is the allocated stereo frame count. */
int music_add_pcm_capacity(MusicProject *project, const char *name, int16_t *owned_stereo,
                           uint32_t frames, uint32_t capacity, uint32_t offset, char error[256]);
int music_import_wav(MusicProject *project, const char *name,
                     const unsigned char *data, size_t size, char error[256]);
/* Requires a valid project whose controls/storage remain stable during the call. */
void music_render(const MusicProject *project, uint32_t start, uint32_t frames, int16_t *stereo);
int music_export_wav(const MusicProject *project, MusicWrite write, void *context, char error[256]);
int music_save(const MusicProject *project, MusicWrite write, void *context, char error[256]);
int music_load(MusicProject *project, const unsigned char *data, size_t size, char error[256]);
int music_demo(MusicProject *project, char error[256]);
#endif
