"""Summarise an Unreal CSV-profiler capture: frame/game/render/GPU times and the most expensive GPU passes.

    python -I Scripts/perf_report.py Saved/Perf/run.csv [log] [skip_seconds]

Skips the first 3 s (loading, shader warm-up), prints percentiles, the route split into ten slices (where it gets
slow) and the GPU passes ranked by mean cost. Hitches (frames > 3x median) are listed with the time they happened.
"""
import csv
import statistics
import sys

csv.field_size_limit(1 << 30)


def num(v):
    try:
        return float(v)
    except ValueError:
        return None


def load(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        rows = list(csv.reader(f))
    head = rows[0]
    cols = {h: i for i, h in enumerate(head)}
    fi = cols["FrameTime"]
    data = [r for r in rows[1:] if len(r) >= len(head) and num(r[fi]) is not None]
    return head, cols, data


def series(data, cols, name):
    i = cols.get(name)
    if i is None:
        return None
    return [num(r[i]) or 0.0 for r in data]


def pct(v, p):
    s = sorted(v)
    return s[min(len(s) - 1, int(p / 100 * len(s)))]


SKIP = 3.0


def main():
    global SKIP
    if len(sys.argv) > 3:
        SKIP = float(sys.argv[3])
    head, cols, data = load(sys.argv[1])
    ft = series(data, cols, "FrameTime")
    t, keep = 0.0, []
    for i, x in enumerate(ft):
        t += x / 1000
        if t > SKIP:
            keep.append(i)
    data = [data[i] for i in keep]
    ft = series(data, cols, "FrameTime")
    print(f"frames {len(ft)}  duration {sum(ft) / 1000:.1f}s  mean fps {1000 * len(ft) / sum(ft):.1f}")
    for name in ("FrameTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime", "GPUTime"):
        v = series(data, cols, name)
        if v:
            print(f"  {name:18s} mean {statistics.mean(v):6.2f}  p50 {pct(v, 50):6.2f}  p95 {pct(v, 95):6.2f}  p99 {pct(v, 99):6.2f} ms")
    gt, rt, gpu = (series(data, cols, n) for n in ("GameThreadTime", "RenderThreadTime", "GPUTime"))
    n = len(ft)
    print("slices (fps p50 | game / render / gpu ms p50):")
    for k in range(10):
        a, b = k * n // 10, (k + 1) * n // 10
        sl = ft[a:b]
        t0 = sum(ft[:a]) / 1000 + SKIP
        print(f"  {t0:6.1f}s  {1000 / statistics.median(sl):6.1f} fps | {statistics.median(gt[a:b]):5.2f} / "
              f"{statistics.median(rt[a:b]):5.2f} / {statistics.median(gpu[a:b]):5.2f}")
    med = statistics.median(ft)
    hitches = [(sum(ft[:i]) / 1000 + SKIP, x) for i, x in enumerate(ft) if x > max(3 * med, 50)]
    print(f"hitches > {max(3 * med, 50):.0f} ms: {len(hitches)}  " + "  ".join(f"{a:.1f}s:{b:.0f}" for a, b in hitches[:25]))
    gpucols = [h for h in head if h.startswith("GPU/") and h != "GPU/Total"]
    ranked = sorted(((statistics.mean(series(data, cols, h)), h) for h in gpucols), reverse=True)
    print("GPU passes (mean ms):")
    for m, h in ranked[:28]:
        if m < 0.05:
            break
        print(f"  {m:6.2f}  {h[4:]}")
    other = [h for h in head if h.startswith(("RenderThread/", "Exclusive/GameThread/", "Basic/"))]
    if other:
        ranked = sorted(((statistics.mean(series(data, cols, h)), h) for h in other), reverse=True)
        print("CPU stats (mean ms):")
        for m, h in ranked[:22]:
            if m < 0.1:
                break
            print(f"  {m:6.2f}  {h}")
    for h in ("DrawCall/Total", "DrawCalls", "RHI/DrawCalls", "RHI/PrimitivesDrawn"):
        v = series(data, cols, h)
        if v:
            print(f"  {h}: mean {statistics.mean(v):.0f}")


if __name__ == "__main__":
    main()
