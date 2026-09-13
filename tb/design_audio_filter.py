"""Regenerate/check the ADC FIR coefficients (design tool: NumPy/SciPy)."""
import argparse
from pathlib import Path

import numpy as np
from scipy.signal import freqz, remez

parser = argparse.ArgumentParser()
parser.add_argument("--write", action="store_true")
args = parser.parse_args()
taps = remez(321, [0, 3400, 4006, 32048], [1, 0], weight=[1, 10], fs=64096)
quantized = np.rint(taps / taps.sum() * 524288).astype(int)
quantized[160] += 524288 - quantized.sum()
frequency, response = freqz(quantized / 524288, worN=32768, fs=64096)
ripple = np.ptp(20 * np.log10(abs(response[frequency <= 3400])))
rejection = -20 * np.log10(max(abs(response[frequency >= 4006])))
assert ripple < 0.09 and rejection > 65
assert np.array_equal(quantized, quantized[::-1])
assert max(abs(quantized)) < 131072
print(f"Quantized FIR: passband ripple {ripple:.3f} dB; stopband rejection {rejection:.2f} dB")
lines = "\n".join(
    f"        9'd{i}: coefficient = {'-' if v < 0 else ''}18'sd{abs(int(v))};"
    for i, v in enumerate(quantized[:161])
)
path = Path(__file__).resolve().parents[1] / "rtl/next/next_audio_adc.sv"
text = path.read_text()
start = text.index("// BEGIN GENERATED FIR COEFFICIENTS")
start = text.index("\n", start) + 1
end = text.index("// END GENERATED FIR COEFFICIENTS", start)
if args.write:
    path.write_text(text[:start] + lines + "\n" + text[end:])
else:
    assert text[start:end] == lines + "\n", "RTL coefficients differ; run --write to regenerate"
