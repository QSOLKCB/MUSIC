# Physical-device acceptance

**Status: UNEXECUTED on Linux, macOS, and Windows.** This document is a manual
procedure, not a test report. No physical microphone, speaker, permission prompt,
disconnect, latency, or endurance result is established by adding it. Populate a
separate results record for each OS, build, and device pair actually tested.
Historical automated checks are recorded in [VALIDATION.md](VALIDATION.md).

The null-backend smoke test, allocation seam, successful compilation, and a
portable archive's existence do not establish real-device support. Do not run
with `--smoke` for these checks: that deliberately uses a null device.

## Scope and evidence

- Test the exact commit and delivered application, not a different development
  executable. Record its commit, version, artifact SHA-256, build configuration,
  and launch method. Keep the original artifact and fixtures unchanged.
- Record OS/version/architecture, GTK version, audio service/backend if known,
  input/output make and model, connection type, driver version if available,
  default devices, device sample-rate settings, and any resampling or direct
  monitoring configured outside MUSIC. Unknown values must remain unknown.
- MUSIC uses the OS default input/output. It has no device selector, latency
  calibration, automatic recovery guarantee, or live software input monitoring.
  Its requested internal stream is stereo 48 kHz, signed 16-bit PCM; a device or
  OS may convert its native format. Do not infer the hardware format from this.
- Use `PASS`, `FAIL`, `BLOCKED`, or `UNEXECUTED` per case. A pass is limited to the
  recorded configuration and steps. Log exact errors, steps to reproduce,
  timestamps, project/export filenames, and relevant screenshots or logs. A
  skipped measurement is not zero latency, zero drift, or zero dropouts.
- Preserve evidence locally and use synthetic signals or recordings made with
  consent. Review captures, names, and screenshots before sharing them.

## Prepare safely

1. Use a disposable project folder and backups of useful recordings. This
   preview has no autosave/crash recovery. Do not test interruption on the only
   copy of a project.
2. Use headphones at low output volume and a conservative input gain. Avoid
   clipping and speaker-to-microphone feedback. Disable interface direct
   monitoring and OS "listen to input" features for the no-monitoring check;
   otherwise sound heard there is not evidence of MUSIC monitoring.
3. Set the intended input/output as defaults in OS audio controls before launch.
   Check both devices independently with the OS's normal audio tools, then close
   competing recorders. Note exclusive-mode or device-sharing restrictions.
4. Prepare short, non-clipping mono/stereo 16-bit PCM WAVs at 44.1 and 48 kHz,
   including a recognizable stereo signal and periodic clicks. Keep their
   hashes and exact durations. Use only compatible line-level connections for
   loopback measurements; never connect a powered speaker output to an input.
   If safe loopback equipment is unavailable, mark that measurement unexecuted.
5. Record a baseline process-memory observation with the same OS measurement
   tool used later. Whole-process RSS/working set includes GTK, device buffers,
   and allocator overhead; it is not MUSIC's PCM allocation counter.

## Platform launch and permission checks (A)

Execute the applicable platform row, then the common denial/retry procedure.

| Platform | Required launch and environment check | Current physical result |
| --- | --- | --- |
| Linux | Use the desktop build described in the README in a real graphical session with an actual default audio device. Record the audio service and session restrictions. Xvfb/null-device results do not substitute. | UNEXECUTED |
| macOS | Launch the built `music.app` as a bundle, for example from Finder, and record its location. Check the built `Contents/Info.plist` contains `NSMicrophoneUsageDescription` and the expected identifier `org.qsolkcb.Music`. Record the identity shown by the OS permission prompt. Testing only the executable inside the bundle or the `./music` wrapper is not bundle-permission acceptance. This source preview depends on installed GTK and is not a signed/notarized self-contained distribution. | UNEXECUTED |
| Windows | Use the desktop build/package documented in the README. For portable acceptance, extract the whole archive into a fresh user-writable folder, including a path with spaces/non-ASCII characters, and launch its documented entry point outside the build shell. Also test on a machine/account without the build toolchain in PATH; an MSYS2 development shell can hide missing runtime dependencies. Record any Windows security or microphone-privacy restriction rather than bypassing it. | UNEXECUTED |

1. Launch without pressing transport controls. Confirm no audio starts and no
   take appears. Load Demo and confirm it also stays stopped.
2. Where the OS offers a first-use microphone prompt, press Record and deny
   permission. On systems without an application prompt, use the relevant OS
   privacy/access controls if available; otherwise mark denial testing blocked
   and state why. Do not claim that Linux must display a prompt.
3. Record the actual result: error text, silent capture, prompt attribution,
   elapsed time, and whether Finish take/Stop remain responsive. Existing
   tracks must remain intact. Silence after denial is not a successful capture;
   flag a misleading recording indication or unusable recovery as a limitation.
4. Restore permission using the OS controls, then retry Record. If the OS needs
   a quit/relaunch, save any existing work first and record that requirement.
   Verify real input is audible in the saved take, not just that the playhead
   moves. On macOS, repeat through the same `.app` bundle and verify permission
   applies to MUSIC, not merely a terminal or another launch host. Do not reset
   permissions for unrelated applications.
5. Quit/relaunch and repeat a short capture with the retained permission state.
   For Windows portable testing, repeat from the extracted package. Record any
   missing DLL, resource, file-dialog, or startup failure as a package failure,
   separate from a microphone/device failure.

## Playback and recording (B)

1. Load Demo; Play, Pause, resume, Stop, rewind (`|‹`), and move the playhead with
   the timeline ruler. Repeat transport actions at least five times. Check
   output and playhead progress, and that stopping releases the stream without
   a hang or continued sound. Test the recognizable stereo fixture to check
   expected channel routing at safe volume.
2. While stopped, exercise gain, mute, solo, clip Start, and trims. Replay and
   verify the audible result matches the arrangement. During device activity,
   project editing should be locked. Log any unexpected edit or crash.
3. In a new empty project, Record 5–10 seconds of an agreed spoken count or
   quiet test signal. Finish take. Confirm one nonempty track is added at the
   starting playhead position. Rewind and play it to verify actual capture.
   Repeat using Stop to finish, and an immediate Record/Finish take sequence.
   An empty take may be discarded; it must not leave a phantom track or hang.
4. Load a saved backing arrangement. Record from the middle while its existing
   tracks play, then Finish take. Confirm the backing remains intact, the new
   take is placed at the starting cursor, and both can play together. Solo the
   take to check that it contains the input rather than a software copy of the
   mix. With headphones, note any acoustic leakage or hardware loopback setting.
5. Start playback first, then press Record and finish the take. The current
   implementation stops the playback device and reopens a duplex device; record
   any audible transition gap. This is not a promise of seamless punch-in.
6. With direct monitoring and OS input listening disabled, Record into an empty
   project while speaking quietly. MUSIC must not route the live input to its
   output. Then Finish take and replay: the input should be audible only on
   replay. Stop immediately if feedback or unexpectedly loud output occurs.

## Repeated short takes and retained capacity (C)

1. In an empty project, record and finish 16 approximately one-second takes.
   Rewind before each take if desired. Record actual durations, track count,
   process memory after each stop, and any error. All 16 short takes should fit
   without an early 256 MiB limit caused by retaining a full ten-minute buffer
   for every successful short take. Attempting a 17th must report the 16-track
   limit; this is not a memory-limit failure.
2. Save, reopen, and play the takes. Remove several tracks, record replacements,
   then repeat a new-project batch. Check that audio is preserved and record/
   stop stays responsive. Report memory growth across batches, including whether
   it stabilizes; do not call an RSS plateau proof of no allocation leak.
3. If an allocation profiler/debugger is available, measure retained requested
   PCM bytes after device shutdown. Successful compaction retains
   `sum(captured_frames × 4)` bytes; exactly 16 one-second takes at 48 kHz total
   3,072,000 bytes. Manual timing will differ. Full capacity is allocated before
   recording, so a larger transient allocation while recording is expected.
4. If shrinking fails, ownership of the take must be preserved and its full
   retained capacity charged against the 256 MiB budget. Do not deliberately
   exhaust a live system to force this case. The automated `audio-capacity` test
   injects that failure; label it automated evidence, not physical-device proof.
   If no profiler was used, mark allocation-byte measurement UNEXECUTED.

Saved project size reflects captured PCM, not spare in-memory capacity. Neither
small project files nor whole-process RSS alone establish correct capacity
accounting. See [ARCHITECTURE.md](ARCHITECTURE.md) for the ownership contract.

## Save, reopen, and export (D)

1. Save a project containing imported audio and real takes, with distinct track
   names (include a non-ASCII name), placement, trims, gain, mute, solo, and BPM.
   Export a stereo WAV. Wait for completion before closing.
2. Quit, relaunch, reopen the `.music` file, and verify all settings and audio.
   Play it on the same physical device. Export again without editing and compare
   the WAV bytes/hashes: the two exports should match. Live recordings from
   different sessions are not expected to match.
3. Inspect the exported format (48 kHz, stereo, 16-bit PCM), duration, and sample
   contents with an independent audio tool. Listen at low volume outside MUSIC.
   Do not confuse a silent/muted mix or a genuinely silent input with a failed
   encoder; preserve the project settings with the evidence.
4. Make an unsaved edit, attempt New/Open/Demo/Close, and cancel the discard
   prompt. Confirm work survives. In the disposable folder, cancel an active
   save/export if it lasts long enough and test an unwritable destination.
   Preserve any prior destination and confirm failures leave it intact. If the
   operation completes before Cancel, mark cancellation unexecuted, not passed.

## Limits and long-run measurements (E)

Run stress checks separately from the short functional checks. Save first and
keep a person available to stop playback safely. Record exact frames/durations
from saved audio; the UI clock alone is not a boundary measurement.

- **10-minute source/take:** In an otherwise empty project at time zero, record
  until automatic completion. It must stop by 28,800,000 captured frames
  (600 seconds). Verify the take is retained and can save/reopen/export. Import
  a valid 600-second supported WAV and reject a source longer than the limit,
  preserving the previous project. If another bound ends recording first,
  this does not establish the ten-minute boundary.
- **30-minute timeline:** Place a short clip so its visible end is exactly
  1,800 seconds using Start and trim fields. Attempts to extend beyond it must
  clamp or reject without corrupting the arrangement. Move the playhead near
  the end and Record until the remaining timeline is exhausted; verify the
  resulting take ends at or before frame 86,400,000. The current stop message
  refers generally to a take or memory limit; log this wording even when the
  timeline bound caused the stop.
- **256 MiB project PCM:** Using disposable supported fixtures, import two
  600-second 48 kHz stereo sources (230,400,000 PCM bytes total), then record at
  time zero. The maximum remaining captured capacity is 9,508,864 frames, about
  198.101 seconds, if both source allocations have no spare capacity. Recording
  should stop at that budget; another nonempty import/take should be refused
  without losing existing work. Remove a source and verify recording works
  again. This is a PCM allocation bound, not a 256 MiB process-memory promise.
- **Latency/drift/dropouts:** With safe loopback, play a known click sequence
  through the actual output and capture it in one take while the backing plays.
  Start at timeline zero. Export the backing-only reference before the test;
  afterward solo the recorded take and export it without changing its placement
  or trims. Keep both WAVs and the project. Measure the captured versus
  reference transient offsets near the start and end, in frames and
  milliseconds (`frames / 48`). Report fixed offset and change in offset over
  the measured interval; label this round-trip path, not isolated input or
  output latency. Run a short check and a full ten-minute check. Record missing
  clicks, discontinuities, audible gaps, CPU load, and the analysis method.
  Human claps or listening alone are not precision latency/drift measurements.
  MUSIC exposes no hardware xrun counter; "none heard" is only an observation.

No latency, drift, or dropout tolerance is promised by this preview. State the
acceptance threshold chosen before measurement, or report measured values with
no performance-pass claim. Capture placement uses a nominal callback cursor,
not hardware latency compensation or synchronization of separate device clocks.

## Missing devices, disconnects, and recovery (F)

1. Save a disposable project. With no usable default input, attempt Record; with
   no usable default output, attempt Play and Record. If the OS always supplies
   a fallback, record it and mark the no-device case BLOCKED rather than claiming
   it ran. Record initialization errors and verify existing tracks stay intact.
2. Restore the default devices and retry. Note whether restarting MUSIC is
   needed. Default-device selection is delegated to the backend; a successful
   retry does not establish automatic device switching.
3. At low volume, disconnect a safely removable external device once during
   playback and once during a disposable recording. Observe status, playhead,
   audio routing, responsiveness, and the result of Stop/Finish take. A backend
   may switch devices or deliver silence without a useful app warning. Inspect
   captured PCM and log the gap/silence instead of treating cursor motion as
   proof of input. Stop if output unexpectedly moves to loud speakers.
4. Reconnect, restore defaults, and try a fresh short take. Verify any retained
   take can save/reopen. If the app becomes unresponsive, preserve diagnostics
   and mark FAIL; do not claim saved-work recovery for unsaved audio.

Hot-plug continuity, automatic reconnection, mid-stream default-device changes,
and seamless failover are **not established features**. Document observations
and limitations for the tested backend. A disconnect check must never be used
to imply broader device-selection or recovery support.

## Compact results template

Copy this block for each tested OS/build/device pair. Leave an unrun item as
UNEXECUTED; do not replace the document's initial status with a global pass.

```text
Date/time (UTC), tester:
Commit / version / artifact SHA-256:
OS / architecture / GTK / build configuration:
Launch path and method / clean-package environment:
Input / output / drivers / backend / default devices:
Device formats / external monitoring or loopback settings:
Fixture hashes / project and export evidence paths:
Permission prompt identity / denial result / retry or restart needed:

A Launch, permissions, denial/retry: UNEXECUTED
B Playback, capture, backing playback, no monitoring: UNEXECUTED
C Repeated short takes / capacity measurement: UNEXECUTED / UNEXECUTED
D Save, reopen, export, interruption: UNEXECUTED
E 10-minute source/take / 30-minute timeline / 256 MiB: UNEXECUTED / UNEXECUTED / UNEXECUTED
E Latency / drift / dropout measurements: UNEXECUTED / UNEXECUTED / UNEXECUTED
F Missing devices / disconnect / retry: UNEXECUTED / UNEXECUTED / UNEXECUTED

Measured start/end offset (frames and ms), duration, method:
Dropout count or listening-only observation / chosen thresholds:
PCM allocation measurement / process-memory observations and tool:
Failures, blocked/skipped substeps, exact errors, and reproducer:
Overall: UNEXECUTED
Scope accepted, remaining limitations, and next action:
```

Partial completion remains partial: name unexecuted or blocked substeps, even
when the rest of a group passes. Keep build/package validation, automated
regressions, functional device acceptance, and measured audio performance as
separate conclusions.
