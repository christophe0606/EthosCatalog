# MIDI score windows

`ScoreWindows` converts events from a successful SMF parse into independent
1,000-frame model windows. At 250 control frames/s each covers four seconds.
Times round to the nearest 4 ms on the absolute score grid, with ties upward;
the maximum boundary displacement is 2 ms. Quotient/remainder arithmetic avoids
overflow. The final window reports `valid_frames` and pads its score with rest.
This padding affects the noncausal model and needs audio validation.

Each output contains note/rest regions with frame lengths, frame-to-region
mapping, onset/offset flags and per-frame MIDI controllers. Onset and offset
flags describe model regions, including rests. Adjacent reattacks remain
separate even when their pitch is unchanged. `score_frame_features()` provides
pitch/127, onset, offset and the source's non-rest relative position; expression
conditioning remains the learned model's responsibility.

Regions clip at each model boundary. Note serials and carry hints expose real
note continuity to the integration; independent-window preprocessing does not
establish equivalence to whole-piece bidirectional inference. `held_at_end`
describes state before events on the next window boundary. Keep DSP phase,
noise overlap and retained reverb state continuous as appropriate.

The first instrument that actually sounds in a window selects its single
trained instrument stream. Later note-ons for another instrument are rejected
without stealing it. Eligible overlaps within the selected instrument use
last-note priority; FIFO press/release identities keep a stolen or rejected
press's note-off from releasing a newer same-key press. Sustain and controllers
7/10/11/64/120/121/123 and pitch bend are retained. Untrained programs,
percussion channel 10 and MIDI key zero (the model's rest token) are rejected.
Program changes affect subsequent presses. Ordinary rests retain the last
admitted velocity and controller channel, allowing learned release behavior.

The caller supplies the measured region-work limit, up to a storage bound of
64. If a window needs more regions, the frontend retains a bounded prefix and
replaces the suffix with one rest. `capacity_rest_start` identifies that suffix:
the playback integration must hard-mute dry synthesis from this point, because
learned rest controls alone do not guarantee silence. MIDI state keeps advancing;
a held note can continue in a later window whose work fits. Counters report
instrument rejections, overlaps, sub-frame lost notes, dense windows/regions
and capacity-rest duration. None of these storage bounds establishes capacity.

Instrument tails and the cost of instrument changes remain the playback
owner's responsibility. Swapping IR coefficients over another instrument's
retained history is not equivalent to retaining its tail. Apply the measured
stream cap and an explicit tail-stealing/rejection policy.

Host tests pass for timing boundaries, reattacks, FIFO stealing/rejection,
controller/sustain release, carry, density and all thirteen aligned identities.
The state occupies 16,696 bytes and one output 11,048 bytes on the tested host;
verify actual firmware allocation with the build-artefact tools. Prefer static
storage. Reproduce from the repository root with:

```text
python tests/run_midi_ddsp_score_tests.py --vcvars <path-to-vcvars64.bat>
```

The original `single_voice.mid` produces two windows with no rejection. The
original `all_instruments.mid` spaces instruments 4.5 seconds apart, so some
windows contain more than one identity: the one-stream policy rejects 36 note
requests. Use aligned instrument sections when demonstrating all thirteen
identities without admission loss, or report the rejects explicitly.
