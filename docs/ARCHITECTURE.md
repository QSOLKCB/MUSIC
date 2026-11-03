# MUSIC architecture, first audio milestone

`src/music.c` is portable C99. It owns interleaved stereo PCM, arrangement metadata, integer mixing, strict bounded WAV import, and the project codec. It knows nothing about GTK, operating-system devices, networking, or filenames. Stream writers report failure to the caller. The renderer requires a validated, immutable project and performs no allocation.

`src/audio.c` wraps pinned miniaudio device I/O. The main thread freezes project edits before starting the device and joins device callbacks before editing or freeing PCM. Recording storage is allocated before the callback starts, within remaining project limits. The callback mixes output and copies input into that allocation; it does not perform file I/O or allocate memory. Cursor/completion exchange uses miniaudio's pinned portable atomic helpers. No software microphone monitoring is mixed into the speakers.

Stopping a take joins the device before shrinking its allocation to captured frames. Empty takes are freed. If shrinking fails, the original take remains owned and its full allocated frame capacity is charged by `music_memory`, reducing the space available to subsequent recordings/imports. Logical frames alone determine rendering and serialization; spare capacity is never written to projects. The project format is unchanged. Capacity denotes requested PCM allocation bytes, not allocator bookkeeping or whole-process RSS.

`src/main.c` owns the GTK window and file workers. A file operation locks edits until completion. Saves and exports go to a private, exclusive temporary file beside the destination; a complete, closed file replaces the chosen destination. Cancellation and failures remove the temporary output. A failed project read leaves the live project intact. No folder cleanup or source-audio rewriting is performed.

The playback/capture callback's nominal cursor is used for take placement. Real device latency, sample-clock behaviour, dropouts, and microphone permission flows require hardware acceptance testing; the null backend does not establish those properties.

## Project format v1

All integers are unsigned little-endian. No C structure is written directly. The complete file must be consumed; trailing or truncated data is rejected. Audio is signed 16-bit little-endian PCM, interleaved L/R. The container embeds source clips, including trimmed portions, so editing is non-destructive.

| Order | Field |
| --- | --- |
| Header | Eight ASCII bytes `MUSICP01` |
| Header | u32 sample rate, required 48000 |
| Header | u32 track count, at most 16 |
| Header | u32 grid BPM, 40–240 |
| Per track | u32 name byte count, 0–127; no embedded NUL |
| Per track | u32 source frame count, 1–28,800,000 |
| Per track | u32 timeline offset in frames |
| Per track | u32 trim start, inclusive; u32 trim end, exclusive |
| Per track | u32 gain in Q16, 0–262144 (0–4×) |
| Per track | u32 flags: bit 0 mute, bit 1 solo; other bits rejected |
| Per track | Name bytes, then `source_frames × 4` bytes of stereo PCM |

The aggregate decoded PCM limit is 256 MiB, and the final arrangement cannot extend past 86,400,000 frames (30 minutes). GTK also bounds file sizes before reading. Names are displayed through GLib UTF-8 repair; filenames and names are data, never shell commands.

## Determinism boundary

Mixing uses signed 64-bit accumulation and fixed-point track gain, then signed division and saturation. This avoids host floating-point differences in rendered PCM. The checked demo WAV is compared byte for byte by its SHA-256 on every core CI platform. UI drawing and mouse placement use floating-point coordinates; recording reflects live hardware and is not deterministic. The 44.1-to-48 kHz converter is basic integer linear interpolation, not a mastering-quality resampler.

## Next implementation order

1. Physical-device acceptance on Linux and Mac; Windows native desktop build and packaging.
2. Undo/redo, multiple clips per track, split/duplicate, zoom/scroll, device selection and latency calibration.
3. Higher-resolution audio path and evaluated resampling, with explicit format migration.
4. MIDI events, piano roll and a local instrument engine.
5. Effects, automation and plugin hosting behind tested boundaries.

Mac is retained throughout; the founding-note pricing joke is not a platform exclusion.
