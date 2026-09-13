# Audio recording

Choose **Audio input → ADC** in the MiSTer OSD to record from the ADC-IN
jack. Digital IO provides the jack on the board; an Analog IO 6.1 setup
uses the separate ADC input adapter. **Silence**, the default, records
silence and allows recording and stopping without an external source.

This implements the NeXT codec's mono, 8-bit mu-law input at 8,012 samples
per second. DSP/SSI recording is a separate path and remains unimplemented.

## Recording lockup

The NeXT-049 hardware capture taken after recording showed two identical
64 MiB RAM snapshots: the clock and scheduler had stopped, with no panic
and no pending SCSI command. The active audio-input stream was stopping
its DMA channel at CSR address `0x02000080`.

Mach 3.3's `_dma_abort` raises the interrupt mask to level 6. On machine
model `0x139` it polls the DMA CSR until it is nonzero, then writes RESET.
Sound-input DMA previously went to `next_dma_stub`, whose CSR always
returned zero, even after SETENABLE. That polling loop could never finish.

`next_snd_in.sv` now implements the channel's enable, chaining, completion,
reset, saved-limit and pointer registers. Stopping the codec stops new
samples while retaining DMA status for the abort routine. Completion is
routed to interrupt-controller bit 22. DMA writes use the existing RAM
arbiter and CPU cache snoop path, retaining the full address until physical
RAM validation so invalid pointers cannot alias into guest RAM.

CLRCOMPLETE is conditional, as in the repaired SCSI channel: a stopped
completion remains visible until RESET or explicit reenable. A new
completion wins over an acknowledgement on the same clock; RESET wins over
completion. Outstanding memory requests remain stable until their response,
and a response after RESET cannot restore the aborted channel's completion
or overwrite replacement pointer state.

KMS input request/overrun status now reflects the input channel. The shared
sound overrun interrupt combines input and output sources, so clearing one
does not hide the other.

## ADC path

`next_audio_adc.sv` uses the framework's `ltc2308` controller, channel 0,
in the existing 28 MHz system clock domain. It samples at 64,096 Hz,
removes the input's DC bias, and applies a 321-tap low-pass FIR before
decimating by eight and passing signed PCM to the codec. The quantized
filter preserves 0–3,400 Hz with less than 0.09 dB ripple and rejects
4,006–32,048 Hz by more than 65 dB. Its group delay is approximately 2.5 ms.
The codec converts samples to mu-law and packs
four samples, oldest first, into each DMA memory word. Silence encodes
as `ff ff ff ff`.

The ADC controller's sample toggle is initialized on reset, making its
startup deterministic. The input filter discards the startup conversion
pipeline and acquires the DC bias before emitting audio. The ADC and DMA
use the same clock domain.

The ADC interface uses 7 MHz SCK and holds CONVST high for 2 microseconds.
It waits another two system clocks after CONVST falls before capturing
the MSB. `NeXT.sdc` constrains the FPGA portions of the external I/O paths;
`tb/check_adc_timing.tcl` checks their fitted timing at every available corner.
See [audio fixes](AUDIO_FIXES.md) for the timing budget and validation.

MiSTer's [ADC test core](https://github.com/MiSTer-devel/ADCTest_MiSTer)
demonstrates full-amplitude ADC audio sampling. This path uses the raw
12-bit samples, rather than the framework's tape threshold output.
The codec rate, packing and KMS commands follow the local Previous
reference's `src/snd.c`, `src/kms.c` and `src/dma.c`.

## Validation

`./tb/run_tests.sh` includes:

- `tb_next_snd_in`: the captured 128-byte input window; sample pacing and
  byte contents; stop/restart and abort; chaining and late acknowledgement;
  same-clock completion/ack/reset; pending RAM responses during reset;
  signed mu-law endpoints; invalid addresses; independent input/output
  overrun acknowledgement.
- `tb_next_audio_adc`: an LTC2308 serial model checks 12-bit sample capture,
  DC removal, positive/negative amplitude and reset with a different bias.
- `tb_next_recording`: the real AP68040 and `next_system` execute a small
  ROM program that records to RAM, waits for system interrupt bit 22, and
  executes the captured CSR polling/RESET instruction sequence. It also
  aborts a newly armed channel with the codec already stopped.

Against the pre-fix system and KMS RTL, `tb_next_recording +abortonly`
stalls at the relocated polling instruction, PC `0100014a`, until the test
times out. The fixed RTL returns from both abort paths and checks all 128
recorded bytes. The complete device suite and both ROM boot smoke tests
also pass. The tests model ADC input; live recording from the physical
jack still needs a hardware test.

Local capture/reproduction artifacts are in
`tb/build/live_20260911_lockup_2348/`. Build and regression logs are in
`tb/build/sndin_fix/`.

## September 12 test build

The recording fix and ADC input are included in
[`NeXT_20260912_recording_adc_fix.rbf`](../releases/NeXT_20260912_recording_adc_fix.rbf)
(4,438,312 bytes), built with Quartus 17.0. Full compilation completed with
zero errors. `tb/check_timing.sh` passed: all reported setup, hold, recovery,
removal and minimum-pulse-width slacks are nonnegative. Worst setup slack
is +0.544 ns, worst hold slack is +0.243 ns, and the 28 MHz core's setup
slack is +6.378 ns.

SHA-256: `3797a1a23a5132a7646720cbf55e4001da7f829b6d03262b6dd825f5b362166e`.

The build's source fingerprints, copied timing summary and RBF checksum
are saved alongside the logs in `tb/build/sndin_fix/`. This is a hardware
test build; the physical ADC jack has not yet been tested with it.
