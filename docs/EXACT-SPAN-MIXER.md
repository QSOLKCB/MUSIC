# Exact-span mixer: OPT transfer

## Provenance and scope

Adapted the **static-state hoisting and sparse hot-loop structure**, not donor code,
from [OPT-DSP-001](https://github.com/QSOLKCB/OPT/blob/7556dd36acd53ed3ed538fd03b1db44e5c1ec869/optimizations/OPT-DSP-001-control-rate-sparse-vector-dsp.md).
OPT and MUSIC use Apache-2.0. Existing MUSIC notices and LICENSE are retained.
No SPECTRAL/NumPy implementation, control-decimation approximation, native SIMD,
E8 model, dependency, or donor performance claim is imported.

Baseline: MUSIC `25228d4f943862c5dde526e381a26703f60cde4e`, including the
recording-capacity fix. `tests/render_reference.c` freezes that commit's scalar
renderer (function renamed only). It is test/benchmark code, not a runtime switch.

## Optimization contract

- Search space: scalar per-sample checks versus exact active-track spans; local,
  categorical, sequential, noisy CPU-time objective, cheap synthetic evaluation.
- Feasible set: C99, at most 16 tracks, no new dependency/heap allocation/locks,
  at most 16 pointer/gain records of stack scratch.
- Objective: reduce render CPU time without changing any output PCM sample.
- Correctness: same valid-project domain, same frame order, track-order int64 sums,
  division toward zero once after summation, and final signed-16 saturation.
  No gain rounding, intermediate clipping, fast-math, or audio-quality tradeoff.
- Budget: two candidate variants, one initial seven-pair dense sweep, one final
  seven-pair sweep over 36 declared shapes. No adaptive runtime tuning.
- Stop: reject a mismatch; retain only a candidate with material dense multitrack
  benefit and no material representative regression. Timings are not CI gates.

The original inner loop checks mute/solo/offset/trim for each sample and track.
The new loop ends a span at the next exact clip start/end. It computes the active
list once for that span, preserving project track order. Each frame still reads
and sums in the old order. Empty spans are zero-filled. At most 32 clip boundaries
exist in a call; scratch is bounded independently of requested frames.
`uint64_t` positions preserve behavior when a render request crosses UINT32_MAX.
The full sum for valid projects is bounded by 16 * 32768 * (4 * 65536), safely
inside int64. Control values are rebuilt each call; there is no persistent cache.

The project is already immutable during active playback/recording: `audio.c`
requires immutability until device join, and `main.c` rejects active-device edits.
This does not introduce sample-accurate automation. Unsynchronized concurrent
project mutation is not supported by either renderer. Between-call edits remain
visible immediately. Existing frame-order sample-buffer alias behavior is tested.

## Validation

`render-equivalence` is a normal CTest target: 2,000 seeded valid projects, random
trim/offset/gain/mute/solo combinations, empty projects/requests, seek and clip
edges, uint32-crossing positions, maximum positive/negative sums, cancellation
before clipping, muted solo, changing controls between calls, split-call equality,
output sentinels and overlapping source/output. Oracle comparisons are byte-exact.
Existing core and audio-capacity tests are unchanged and remain required.

Local GCC strict C99 Release build passes. CMake Debug ASan/UBSan passes all three
CTest targets; local LeakSanitizer disabled because this workspace uses ptrace.
Physical capture, speakers, underruns and end-to-end latency were not measured.

## Reproduction and measurements

From repository root (no user audio or network needed):

```sh
cc -O3 -std=c99 -Wall -Wextra -Wpedantic -Werror -Isrc \
  src/music.c tests/test_render.c tests/render_reference.c -o /tmp/music-render-tests
/tmp/music-render-tests --benchmark > /tmp/music-render-benchmark.csv
cmake -S . -B build-core -DMUSIC_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Both renderers use identical compiler/flags in separate translation units, without
LTO/native-ISA/fast-math flags. `clock()` measures process CPU time, **not wall-clock
callback latency**. Each observation renders 12,000 blocks; seven pairs alternate
reference-first/candidate-first order. Sizes: 64/256/1024 frames. Track counts:
0/1/8/16. Modes: 0 all tracks active; 1 three quarters muted; 2 short staggered
clips with gaps. The final sweep includes setup per render call and a volatile
output checksum, but excludes fixture construction, device and GUI work.

Raw final observations: [benchmark.csv](evidence/exact-span-mixer/benchmark.csv).
Environment/source identities and per-shape medians/ranges:
[receipt.json](evidence/exact-span-mixer/receipt.json).
Host: Linux x86-64 workspace, reported AMD EPYC 9V74, GCC 14.2.0. Shared-host noise,
no affinity isolation; this is not a Pi/Windows/Mac performance measurement.
Dense 8/16-track median ratios were about **1.54–2.50x reference/candidate** on this
host; single-track differences were smaller (1.10–1.23x). These are synthetic
render-kernel observations, not application-wide speedups or latency guarantees.
Across all 36 shapes the lowest median ratio was 1.104x (dense one-track,
256-frame blocks); none had a median regression. Individual pairs were noisy:
the worst pair was 0.933x (dense eight-track, 64 frames, repeat 4), about 7.2%
more candidate CPU time. Raw ranges are retained, not hidden by aggregate claims.
Sparse/gap cases benefit more, but are deliberately not the headline claim.

The initial candidate zeroed empty spans sample-by-sample and showed a noisy
empty-64-frame regression (~0.83x); its raw observations are retained as
[initial-candidate.csv](evidence/exact-span-mixer/initial-candidate.csv). The final
candidate uses exact bulk zero fill; only final results describe shipped code.
No further tuning or threshold-based promotion is built into the application.

Rollback if parity fails on any supported target, allocation/synchronization is
introduced into rendering, or representative target-host measurements regress.
Keep the original oracle and negative measurement evidence when revising this code.
