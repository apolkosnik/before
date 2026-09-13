# Audio fixes following the September 12 audit

The [audit](AUDIO_AUDIT.md) found defects in playback reset, KMS reset,
sample-rate selection, guest volume, recording filtering, ADC timing
constraints, and S/PDIF rate metadata. The changes here address those seven
findings. DSP56001 support and the separate KMS `C7` direct-output command
remain outside the DMA audio implementation.

## Playback and control

Sound-output DMA now marks cancelled RAM requests and keeps their bus payload
stable until ACK or error. Cancelled responses cannot enter the FIFO, move
pointers, reload a chain, or raise a completion interrupt. Reset also wins
when ACK/error arrives on the reset clock. A DMA reset or ordinary stop
preserves already-accepted samples so short sounds can finish. A KMS reset or
fresh playback start clears the queue and output processing pipeline. Underrun
waits until the FIFO and final repeat/zero slot have drained.

KMS `FF/FFFFFFFF` resets interface status and stops both codec directions,
without clearing DMA descriptor registers. It discards a pending playback
response; playback can restart from the preserved descriptor. Executing KMS
commands now correctly assembles both bytes of the final 16-bit data write.
Other data with command `FF` does not reset the interface.

Normal playback consumes one stereo frame per 44.1 kHz output tick. Commands
`1F` and `3F` consume one frame per two ticks, repeating the frame or inserting
zero respectively. The final repeat/zero slot is emitted even after fetching
the descriptor's final frame has completed DMA.

The legacy `C4` serial volume protocol and `C2` direct volume command select
independent channel attenuation in 2 dB steps, with setting 43 and above
muting the selected channel. Invalid/overlong serial transactions cannot
wrap into a valid command. GPO mute and de-emphasis affect the output before
MiSTer processing. The filter uses the Previous reference coefficients with
Q12 history and Q17 coefficients; volume uses Q16 gains, symmetric rounding,
and saturation. The three-cycle arithmetic pipeline keeps multipliers out
of the direct FIFO-to-output path.

## Recording

The ADC still samples channel 0 at 64,096 Hz and produces mono 8,012 Hz PCM
for mu-law recording. An integer 321-tap FIR replaces the eight-sample
average. Its quantized response has 0.082 dB passband ripple through 3,400 Hz
and at least 66.05 dB rejection from 4,006 Hz upward. The approximately
2.5 ms group delay is appropriate for the recording path.

One multiply/accumulate pipeline processes each output sample. A 512-word
history ring permits continuing ADC writes while the filter reads a snapshot.
Unfilled history is treated as zero after reset, so history RAM needs no
hardware reset. Signed output is rounded and saturated, including filter
overshoot near the ADC rails. `tb/design_audio_filter.py` regenerates and
checks the Q19 coefficients using NumPy/SciPy; those tools are not needed
to synthesize the checked-in coefficients.

The serial ADC-to-DMA probe measures a 5 kHz alias at about **−65 dB**, improved
from −6.48 dB with the original average. A 1 kHz input measures +0.07 dB.
Byte packing and mu-law behavior are unchanged.

## ADC and MiSTer output timing

At the real 28 MHz core clock, ADC SCK is now 7 MHz. CONVST remains high for
2 microseconds, longer than the LTC2308's maximum 1.6 microsecond conversion
time. Two clocks, approximately 71.4 ns, separate CONVST falling from the
first SDO capture, and successive SCK edges have the same spacing.

`NeXT.sdc` applies 18 ns maximum output Tco and input Tsu requirements,
referenced to the same system clock, with zero minimum-delay constraints. SCK is
gated and starts on varying phases of the fractional sample divider, so the
constraints use explicit path budgets instead of a fictitious continuous
generated clock. The round-trip budget is:

| Portion | Maximum allowance |
|---|---:|
| Clock reference to FPGA output (Tco) | 18 ns |
| Board, outward | 2 ns |
| ADC SDO enable/data valid | 15 ns |
| Board, return | 2 ns |
| FPGA input to capture clock reference (Tsu) | 18 ns |
| Total | 55 ns |

The 71.4 ns interval leaves over 16 ns beyond this budget. These are
clock-referenced requirements, not physical routing delays: Quartus 17 adds
launch clock insertion to output Tco and subtracts capture clock insertion
from input Tsu. Adding the two requirements includes the round-trip clock
skew. The initial constraints incorrectly treated 12 ns max-delay exceptions
as 12 ns bounds on routing alone; the fitted reports exposed this error.
The corrected constraints were validated against the same fitted netlist;
the RTL and resulting bitstream did not change during this timing analysis.

The board allowance is an assumption for the DE10-Nano's on-board ADC interface. A serial model
with these propagation delays checks conversion wait, channel configuration,
SDI hold, SCK spacing, and 100 distinct captured words. This and fitted timing
do not substitute for measuring physical analog quality or receiver behavior.

MiSTer's S/PDIF transmitter now receives the selected sample rate and emits
channel-status field `0x2` at 48 kHz and `0xA` at 96 kHz. The I2S/HDMI and
analog output routing are unchanged.

## Validation

`./tb/run_tests.sh` passes, including the existing device tests, boot smoke
tests, and real-CPU recording/abort tests. Additional checks cover:

- Reset during pending DMA, simultaneous ACK/error and reset, replacement
  descriptors, invalid KMS reset data, and restart after KMS reset.
- Distinct stereo frames in all three playback modes, including the final
  repeated/zero frame, and correct source consumption rates.
- Every attenuation setting against floating-point gain, signed endpoints,
  mute, and a reset during arithmetic processing; error is at most one code.
- De-emphasis impulse and 10 kHz tone against Previous's floating-point
  coefficients; error is at most two codes.
- All 65,536 signed PCM-to-mu-law codes and 2,024 tone-driven ADC-to-DMA words.
- 54,964 I2S channel words, 48/96 kHz output rates, stereo tone separation,
  and matching S/PDIF metadata.
- ADC serial transfers with worst-budget propagation and ADC data-valid delays.

An additional timing analysis covered all available operating corners. The
audio and ADC paths pass; it also exposed a 0.009 ns HDMI setup miss at the
cold slow corner. HDMI logic, clocks, and constraints are unchanged. The
project retains its original build settings and release timing gate.

Run `sh tb/run_audio_audit.sh` for the end-to-end diagnostic probes. It now
requires zero failed checks and at least 60 dB measured 5 kHz alias rejection.
Logs are in `tb/build/audio_audit/`; full regression/build logs are in
`tb/build/audio_fixes/`.

After a full Quartus build, run both:

```sh
tb/check_timing.sh
quartus_sta -t tb/check_adc_timing.tcl
```

The ADC check fails if any port has no timed path, negative setup/hold slack,
an unexpected clock reference, or a different delay requirement at any
available operating corner. All 56 ADC checks pass at the four available
corners. The serial test also passes 100 conversions at the full 55 ns
round-trip budget. Full compilation completed with zero errors, using
39,315 of 41,910 ALMs (94%) and 65 of 112 DSP blocks.

The generated test bitstream is `output_files/NeXT.rbf`. It has not been
staged as a release because the all-corner analysis reports the HDMI miss
above. The original build fingerprints are in
`tb/build/audio_fixes/build_sources.sha256`; the corrected ADC constraints
are captured in `final_analysis_sources.sha256` alongside the reports.
No physical jack or receiver test has been performed for these changes.
