"""Summarizes a WAV recorded with NCAABB10_AUDIO_DUMP: per half second, the RMS and peak of the
front left/right and center channels, plus silent stretches, clipping and sample discontinuities.
usage: python -I audio_check.py capture.wav"""
import array
import math
import struct
import sys

with open(sys.argv[1], 'rb') as f:
    data = f.read()
channels, rate = struct.unpack_from('<HI', data, 22)
size = struct.unpack_from('<I', data, 40)[0]
samples = array.array('f')
samples.frombytes(data[44:44 + (min(size, len(data) - 44) // 4) * 4])
frames = len(samples) // channels
print(f'{frames / rate:.1f} s, {channels} channels, {rate} Hz')
chunk = rate // 2
silent_run = 0
longest_silence = 0
clipped = 0
jumps = 0
prev = [0.0] * channels
for start in range(0, frames - chunk + 1, chunk):
    sums = [0.0] * 3
    peaks = [0.0] * 3
    for i in range(start, start + chunk):
        base = i * channels
        for k, c in enumerate((0, 1, 2)):
            v = samples[base + c]
            sums[k] += v * v
            if abs(v) > peaks[k]:
                peaks[k] = abs(v)
            if abs(v) >= 1.0:
                clipped += 1
            if abs(v - prev[c]) > 0.5:
                jumps += 1
            prev[c] = v
    rms = [math.sqrt(s / chunk) for s in sums]
    if max(rms) < 1e-4:
        silent_run += 1
        longest_silence = max(longest_silence, silent_run)
    else:
        silent_run = 0
    if (start // chunk) % 10 == 0:
        print(f'{start / rate:6.1f}s  rms L {rms[0]:.3f} R {rms[1]:.3f} C {rms[2]:.3f}  '
              f'peak {max(peaks):.2f}')
print(f'longest silence {longest_silence * 0.5:.1f} s, clipped samples {clipped}, '
      f'jumps > 0.5 {jumps}')
