"""Measure captured RTL waveforms; requires NumPy. Run from repository root."""
import json
from pathlib import Path

import numpy as np

work = Path("tb/build/audio_audit")
results = {"capture": [], "mister": []}
for frequency in (1000, 5000):
    samples = np.loadtxt(work / f"capture_{frequency}.csv")
    centered = samples - samples.mean()
    spectrum = abs(np.fft.rfft(centered * np.hanning(len(samples))))
    peak = np.argmax(spectrum[1:]) + 1
    rms = np.sqrt(np.mean(centered**2))
    result = {
        "input_hz": frequency,
        "output_peak_hz": float(peak * 8012 / len(samples)),
        "pcm_rms": float(rms),
        "gain_db": float(20 * np.log10(rms / (512 * 8 / np.sqrt(2)))),
    }
    results["capture"].append(result)
    print(f"ADC {frequency} Hz: peak {result['output_peak_hz']:.2f} Hz, "
          f"gain {result['gain_db']:.2f} dB")
    if frequency == 1000:
        assert abs(result["gain_db"]) < 0.2, "recording passband lost gain"
    else:
        assert result["gain_db"] < -60, "recording alias rejection below 60 dB"

for rate in (48000, 96000):
    stereo = np.loadtxt(work / f"mister_{rate // 1000}.csv", delimiter=",")
    t = np.arange(len(stereo)) / rate
    basis = np.column_stack([
        np.sin(2 * np.pi * 1000 * t), np.cos(2 * np.pi * 1000 * t),
        np.sin(2 * np.pi * 2000 * t), np.cos(2 * np.pi * 2000 * t),
        np.ones(len(t)),
    ])
    fit = np.linalg.lstsq(basis, stereo, rcond=None)[0]
    amplitude = np.hypot(fit[0:4:2], fit[1:4:2])
    for channel, frequency in enumerate((1000, 2000)):
        result = {
            "rate_hz": rate, "channel": "LR"[channel],
            "tone_hz": frequency,
            "amplitude": float(amplitude[channel, channel]),
            "opposite_tone_amplitude": float(amplitude[1 - channel, channel]),
        }
        results["mister"].append(result)
        print(f"MiSTer {rate} Hz {result['channel']}: {frequency} Hz amplitude "
              f"{result['amplitude']:.2f}; opposite tone {result['opposite_tone_amplitude']:.2f}")
        assert amplitude[channel, channel] > (3500 if channel == 0 else 1700)
        assert amplitude[1 - channel, channel] < 50

(work / "measurements.json").write_text(json.dumps(results, indent=2) + "\n")
