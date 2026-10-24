"""wet = (mix on) - (mix off): the delay's output alone, from two deterministic runs. Jumps in its read point show
as second-difference spikes far above a smooth delayed sine's."""
import struct, sys, math
def load(p):
    b = open(p, "rb").read(); n = struct.unpack_from("<I", b)[0]; x = struct.unpack_from(f"<{n}f", b, 4)
    m = struct.unpack_from("<I", b, 4 + 4 * n)[0]; c = struct.unpack_from(f"<{m}I", b, 8 + 4 * n); return x, c
for name in sys.argv[1:]:
    on, chg = load(f"{name}_on.raw"); off, _ = load(f"{name}_off.raw")
    n = min(len(on), len(off)); w = [on[i] - off[i] for i in range(n)]
    a, b0 = 5 * 44100 + 66150, n
    d2 = [abs(w[i] - 2 * w[i - 1] + w[i - 2]) for i in range(a, b0)]
    srt = sorted(d2); p99 = srt[int(len(srt) * 0.99)]
    rms = math.sqrt(sum(v * v for v in w[a:b0]) / (b0 - a))
    clicks, k = [], 0
    while k < len(d2):
        if d2[k] > 6 * p99: clicks.append(k + a); k += 300
        else: k += 1
    near = sum(1 for c in clicks if any(abs(c - t) <= 300 for t in chg))
    big = max(d2) / p99 if p99 else 0
    print(f"{name}: wet rms {rms:.3f}; beat changes after 5 s: {sum(1 for t in chg if t >= a)}; "
          f"jumps (|d2| > 6x its 99th percentile): {len(clicks)}, {near} at a beat change; largest {big:.0f}x")
