#!/usr/bin/env python3
"""Record Aether-gate's /diversity status to a CSV, one row per poll.

The combiner's behaviour over hours -- how often it beat the better loop,
when the fade guard switched, which talkers came back, whether the passband
phase ever sloped -- is what decides the next algorithm work. This polls the
gate's control port and appends one flat row per sample; diversity_report.py
turns the file into a summary.

Usage:
    python3 tools/diversity_recorder.py [--host 127.0.0.1] [--port 8731]
                                        [--interval 2.0] [--out FILE]
Stops on SIGINT/SIGTERM. A gate that stops answering is logged as a row
with available=0 and polling continues, so a restart mid-night is visible
in the data rather than fatal to it.
"""
import argparse
import csv
import json
import os
import signal
import sys
import time
import urllib.request

FIELDS = [
    "time", "available", "mode", "source", "pan", "talking", "talk_mod",
    "snr_a", "snr_b", "snr_out", "gain_db", "phase_deg", "ratio_db",
    "talker_id", "talker_since_s", "memory_n", "updates", "rn_source",
    "noise_coherence", "steady_qrm", "pb_flatness", "pb_slope", "pb_coherence",
    "nb_enabled", "nb_pct", "aligned", "lag", "corr_peak", "realigning",
    "sources_n", "top_source_coh", "passband_lo_hz", "passband_hi_hz",
]


def fetch(url, timeout=2.0):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return json.load(r)
    except Exception:            # noqa: BLE001 -- any failure is "not answering"
        return None


def row_from(d, m, now):
    if not d or not d.get("available"):
        return {"time": round(now, 1), "available": 0}
    snr = d.get("snr_db") or {}
    a, b, out = snr.get("a"), snr.get("b"), snr.get("out")
    gain = None
    if out is not None and (a is not None or b is not None):
        gain = round(out - max(x for x in (a, b) if x is not None), 2)
    tk = d.get("talker") or {}
    pb = d.get("passband") or {}
    nb = d.get("nb") or {}
    srcs = d.get("sources") or []
    pb_hz = (m or {}).get("passband_hz") or [None, None]
    return {
        "time": round(now, 1), "available": 1,
        "mode": d.get("mode"), "source": d.get("source"), "pan": d.get("pan"),
        "talking": int(bool(d.get("talking"))), "talk_mod": d.get("talk_mod"),
        "snr_a": a, "snr_b": b, "snr_out": out, "gain_db": gain,
        "phase_deg": d.get("phase_deg"), "ratio_db": d.get("ratio_db"),
        "talker_id": tk.get("id"), "talker_since_s": tk.get("since_s"),
        "memory_n": len(d.get("memory") or []), "updates": d.get("updates"),
        "rn_source": d.get("rn_source"), "noise_coherence": d.get("noise_coherence"),
        "steady_qrm": int(bool(d.get("steady_qrm"))),
        "pb_flatness": pb.get("flatness"), "pb_slope": pb.get("phase_slope_deg_per_khz"),
        "pb_coherence": pb.get("coherence"),
        "nb_enabled": int(bool(nb.get("enabled"))), "nb_pct": nb.get("blanked_pct"),
        "aligned": int(bool(d.get("aligned"))), "lag": d.get("lag_samples"),
        "corr_peak": d.get("corr_peak"), "realigning": int(bool(d.get("realigning"))),
        "sources_n": len(srcs),
        "top_source_coh": (srcs[0].get("coherence") if srcs else None),
        "passband_lo_hz": pb_hz[0], "passband_hi_hz": pb_hz[1],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8731)
    ap.add_argument("--interval", type=float, default=2.0)
    ap.add_argument("--out", default=os.path.expanduser("~/diversity-log.csv"))
    args = ap.parse_args()

    base = f"http://{args.host}:{args.port}"
    stop = {"now": False}

    def _stop(*_):
        stop["now"] = True
    signal.signal(signal.SIGINT, _stop)
    signal.signal(signal.SIGTERM, _stop)

    new = not os.path.exists(args.out) or os.path.getsize(args.out) == 0
    with open(args.out, "a", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS)
        if new:
            w.writeheader()
        n = 0
        while not stop["now"]:
            t0 = time.time()
            d = fetch(base + "/diversity")
            # the map is heavier; every 15th poll is enough for the passband
            m = fetch(base + "/diversity/map") if n % 15 == 0 else None
            w.writerow(row_from(d, m, t0))
            fh.flush()
            n += 1
            time.sleep(max(0.0, args.interval - (time.time() - t0)))
    print(f"recorded {n} rows to {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
