#!/usr/bin/env python3
"""Summarise a diversity_recorder.py CSV: what the combiner did over a session.

Usage:  python3 tools/diversity_report.py ~/diversity-log.csv [--markdown]

Every number here is over rows where the gate was answering; "talking"
rows are those the tracker flagged as an over. Gain is OUT minus the better
loop, so a positive mean means the combiner earned something and zero means
it merely matched the better antenna (the honest floor for isotropic noise).
"""
import argparse
import csv
import statistics
import sys
from collections import Counter


def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def load(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def summarise(rows):
    up = [r for r in rows if r.get("available") == "1"]
    if not up:
        return {"rows": len(rows), "up_rows": 0}
    dt = 0.0
    times = [fnum(r["time"]) for r in rows]
    if len(times) >= 2:
        dt = (times[-1] - times[0]) / max(1, len(times) - 1)
    talk = [r for r in up if r.get("talking") == "1"]
    gains = [g for g in (fnum(r["gain_db"]) for r in talk) if g is not None]
    a_best = sum(1 for r in talk if (fnum(r["snr_a"]) or -99) >= (fnum(r["snr_b"]) or -99))
    b_best = len(talk) - a_best
    won = sum(1 for g in gains if g >= 1.0)
    lost = sum(1 for g in gains if g <= -1.0)

    # fade-guard / refit activity: how often the weight moved during overs
    upd = [int(fnum(r["updates"]) or 0) for r in up]
    moves = sum(max(0, b - a) for a, b in zip(upd, upd[1:]) if b >= a)

    # talkers: distinct ids seen live, and how many overs each
    talker_overs = Counter()
    prev = None
    for r in up:
        tid = r.get("talker_id") or ""
        if tid and tid != prev:
            talker_overs[tid] += 1
        prev = tid
    steady_rows = sum(1 for r in up if r.get("steady_qrm") == "1")
    flat = [f for f in (fnum(r["pb_flatness"]) for r in talk) if f is not None]
    slope = [s for s in (fnum(r["pb_slope"]) for r in talk) if s is not None]
    coh = [c for c in (fnum(r["noise_coherence"]) for r in up) if c is not None]
    realigns = sum(1 for a, b in zip(up, up[1:])
                   if a.get("realigning") == "0" and b.get("realigning") == "1")
    lags = Counter(r.get("lag") for r in up if r.get("lag"))
    modes = Counter(r.get("mode") for r in up)
    snr_a = [x for x in (fnum(r["snr_a"]) for r in talk) if x is not None]
    snr_b = [x for x in (fnum(r["snr_b"]) for r in talk) if x is not None]
    snr_o = [x for x in (fnum(r["snr_out"]) for r in talk) if x is not None]

    def med(xs):
        return round(statistics.median(xs), 2) if xs else None

    def mean(xs):
        return round(statistics.fmean(xs), 2) if xs else None

    return {
        "rows": len(rows), "up_rows": len(up), "sample_s": round(dt, 2),
        "hours_recorded": round(len(rows) * dt / 3600, 2),
        "hours_up": round(len(up) * dt / 3600, 2),
        "talk_minutes": round(len(talk) * dt / 60, 1),
        "talk_share_pct": round(100 * len(talk) / len(up), 1),
        "modes": dict(modes),
        "snr_talking_median_db": {"a": med(snr_a), "b": med(snr_b), "out": med(snr_o)},
        "gain_over_best_loop_db": {"mean": mean(gains), "median": med(gains),
                                   "p90": (round(sorted(gains)[int(0.9 * (len(gains) - 1))], 2)
                                           if gains else None)},
        "overs_where_combiner_won_1db_pct": round(100 * won / len(gains), 1) if gains else None,
        "overs_where_combiner_lost_1db_pct": round(100 * lost / len(gains), 1) if gains else None,
        "best_loop_share_pct": {"a": round(100 * a_best / len(talk), 1) if talk else None,
                                "b": round(100 * b_best / len(talk), 1) if talk else None},
        "weight_moves": moves,
        "talkers_heard": len(talker_overs),
        "overs_per_talker": dict(talker_overs.most_common(12)),
        "steady_carrier_minutes": round(steady_rows * dt / 60, 1),
        "noise_coherence": {"median": med(coh), "p90": (round(sorted(coh)[int(0.9 * (len(coh) - 1))], 2)
                                                        if coh else None)},
        "passband_flatness_talking": {"median": med(flat), "min": (round(min(flat), 3) if flat else None),
                                      "share_below_0_7_pct": (round(100 * sum(1 for f in flat if f < 0.7)
                                                                    / len(flat), 1) if flat else None)},
        "passband_slope_deg_per_khz": {"median": med(slope),
                                       "p90_abs": (round(sorted(abs(s) for s in slope)
                                                         [int(0.9 * (len(slope) - 1))], 1)
                                                   if slope else None)},
        "realigns": realigns, "lags_seen": dict(lags.most_common(5)),
    }


def markdown(s):
    if not s.get("up_rows"):
        return f"No gate rows ({s.get('rows', 0)} samples, gate never answered)."
    g = s["gain_over_best_loop_db"]
    m = s["snr_talking_median_db"]
    pb = s["passband_flatness_talking"]
    lines = [
        "## Diversity session report", "",
        f"- Recorded {s['hours_recorded']} h ({s['hours_up']} h with the gate answering), "
        f"one sample every {s['sample_s']} s.",
        f"- Someone was talking {s['talk_minutes']} min ({s['talk_share_pct']}% of the time); "
        f"{s['talkers_heard']} distinct talkers, {sum(s['overs_per_talker'].values())} overs attributed.",
        f"- Median SNR while talking: A {m['a']} dB, B {m['b']} dB, OUT {m['out']} dB.",
        f"- Combiner gain over the better loop: mean {g['mean']} dB, median {g['median']} dB, "
        f"p90 {g['p90']} dB; won by ≥1 dB on {s['overs_where_combiner_won_1db_pct']}% of samples, "
        f"lost by ≥1 dB on {s['overs_where_combiner_lost_1db_pct']}%.",
        f"- Better loop: A {s['best_loop_share_pct']['a']}% of the time, B {s['best_loop_share_pct']['b']}%.",
        f"- Weight moves (refits, recalls, fade switches): {s['weight_moves']}.",
        f"- Noise coherence between loops: median {s['noise_coherence']['median']}, "
        f"p90 {s['noise_coherence']['p90']} (below 0.3 = isotropic, nothing to null).",
        f"- Passband flatness while talking: median {pb['median']}, min {pb['min']}, "
        f"below 0.7 for {pb['share_below_0_7_pct']}% of samples; "
        f"phase slope median {s['passband_slope_deg_per_khz']['median']}°/kHz, "
        f"p90 |slope| {s['passband_slope_deg_per_khz']['p90_abs']}°/kHz.",
        f"- Steady carrier treated as noise for {s['steady_carrier_minutes']} min; "
        f"{s['realigns']} realigns; lags seen {s['lags_seen']}.",
        f"- Modes: {s['modes']}.",
    ]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--markdown", action="store_true")
    args = ap.parse_args()
    s = summarise(load(args.csv))
    if args.markdown:
        print(markdown(s))
    else:
        import json
        json.dump(s, sys.stdout, indent=2)
        print()


if __name__ == "__main__":
    main()
