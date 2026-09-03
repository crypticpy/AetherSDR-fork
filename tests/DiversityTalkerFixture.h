#pragma once

// The payloads the gate grew for per-talker filters, the automatic contour,
// voice splits and the snapped finder -- gate commits 15aca8f, f1d79b6 and
// a72bcf1.
//
// A sibling of DiversityGateFixture.h rather than more of it, for the reason
// that header itself gives for existing: the fixtures are long, and one file
// carrying every payload the window has ever read stops being something a
// person opens to find one. Same namespace, so a test includes whichever it
// needs -- or both -- and the constants read the same either way.
//
// Nothing here opens a port. Every payload was taken from a live gate on 80 m
// rather than invented, then trimmed to the fields the case under it reads.

#include "DiversityGateFixture.h"

namespace DiversityGateFixture {

// --- per-talker filters and voice splits -----------------------------------
// The gate at 15aca8f/f1d79b6/a72bcf1: memory entries carry the filter the
// gate remembers for each station, one of them marked live, and the status
// carries the running voice_splits count. #4 is a station the gate has kept no
// filter for and has no name for either -- the two "nothing here" cases the
// table has to render as something readable rather than as blanks.
inline const QByteArray kDiversityStatusTalkerFilters = R"JSON({"available": true,
    "channels": 2, "mode": "track", "source": "combined",
    "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 42,
    "voice_splits": 4,
    "memory": [
        {"id": 3, "name": "Ted", "phase_deg": 120.0, "ratio_db": 2.0, "age_s": 4.0,
         "first_seen_s": 900.0, "hits": 12,
         "voice": {"centroid_hz": 1350, "low_hz": 300, "high_hz": 2700,
                   "tilt_db": -4.2, "syllabic_hz": 4.1, "over_s": 18.5, "overs": 7,
                   "bands_db": [-30.0, -28.5, -26.0, -24.0, -22.5, -21.0, -19.5,
                                -18.0, -17.0, -16.0, -15.5, -15.0, -14.5, -14.0,
                                -14.0, -14.5, -15.0, -16.0, -17.5, -19.0, -20.5,
                                -22.0, -24.0, -26.0, -28.0, -30.5, -33.0, -36.0,
                                -39.0, -42.0, -45.0, -48.0]},
         "filter": {"low_hz": 300, "high_hz": 2700, "shape": "soft", "auto": true,
                    "auto_eq": false, "contour": false, "threshold_db": 20.0,
                    "live": true}},
        {"id": 4, "name": null, "phase_deg": 30.0, "ratio_db": 0.0, "age_s": 60.0,
         "first_seen_s": 120.0, "hits": 1, "voice": {}, "filter": null},
        {"id": 7, "name": "Ann", "phase_deg": -60.0, "ratio_db": -1.5, "age_s": 12.0,
         "first_seen_s": 400.0, "hits": 5, "voice": null,
         "filter": {"low_hz": 200, "high_hz": 3000, "shape": "sharp", "auto": false,
                    "auto_eq": true, "contour": true, "threshold_db": 26.0,
                    "live": false}}],
    "talker": {"id": 3, "since_s": 4.0},
    "capture": {"active": false, "path": null}})JSON";

// The next poll: the gate decided mid-over that it was no longer hearing Ted,
// split the over, and is now calling it Ann. voice_splits went up by one --
// which is the ONLY evidence of it, because the frequency did not move and
// nobody stopped talking.
inline const QByteArray kDiversityStatusVoiceSplit = R"JSON({"available": true,
    "channels": 2, "mode": "track", "source": "combined",
    "phase_deg": 45.0, "ratio_db": -2.5,
    "lag_samples": 3, "aligned": true, "corr_peak": 0.91,
    "snr_db": {"a": 12.3, "b": 9.8, "out": 15.1}, "updates": 43,
    "voice_splits": 5,
    "memory": [
        {"id": 3, "name": "Ted", "phase_deg": 120.0, "ratio_db": 2.0, "age_s": 1.0,
         "first_seen_s": 902.0, "hits": 12,
         "filter": {"low_hz": 300, "high_hz": 2700, "shape": "soft", "auto": true,
                    "auto_eq": false, "contour": false, "threshold_db": 20.0,
                    "live": false}},
        {"id": 4, "name": null, "phase_deg": 30.0, "ratio_db": 0.0, "age_s": 62.0,
         "first_seen_s": 122.0, "hits": 1, "voice": {}, "filter": null},
        {"id": 7, "name": "Ann", "phase_deg": -60.0, "ratio_db": -1.5, "age_s": 0.0,
         "first_seen_s": 402.0, "hits": 6, "voice": null,
         "filter": {"low_hz": 200, "high_hz": 3000, "shape": "sharp", "auto": false,
                    "auto_eq": true, "contour": true, "threshold_db": 26.0,
                    "live": true}}],
    "talker": {"id": 7, "since_s": 0.5},
    "capture": {"active": false, "path": null}})JSON";

// The gate that snaps candidates to the 500 Hz grid a human tunes on: "hz" is
// the snapped number the row shows and the tune goes to, "hz_raw" is the
// estimate it came from. Both, because a row that showed only the estimate
// would tune you 130 Hz off the dial mark and a row that showed only the snap
// would hide how sure the gate is.
inline const QByteArray kDiversityFinderSnapped = R"({"available": true,
    "span_hz": [3860000.0, 3862000.0], "history_s": 600,
    "activity": [0.0, 0.2, 0.9, 0.4, 0.0, 0.1, 0.6, 0.0],
    "candidates": [
      {"hz": 3860500.0, "hz_raw": 3860372.4, "width_hz": 2700.0, "score": 0.82,
       "snr_db": 6.1, "syllabic": 0.61, "active_s": 184.0, "last_s": 0.0,
       "phase_deg": 141.0, "coherence": 0.70, "ratio_db": -2.1, "gain_db": 1.4},
      {"hz": 3861500.0, "width_hz": 2400.0, "score": 0.55, "snr_db": -1.2,
       "syllabic": 0.44, "active_s": 42.0, "last_s": 12.0,
       "phase_deg": -30.0, "coherence": 0.21, "ratio_db": 0.4, "gain_db": -0.3}
    ]})";

// --- PER TALKER / AUTO CONTOUR --------------------------------------------
// The two blocks the gate grew at 15aca8f/f1d79b6/a72bcf1, spliced into an
// otherwise plain LSB filter so a case about them is not also a case about
// notches, the ANF and the blanker.
inline QByteArray makeDiversityFilterTalker(const QByteArray& contour,
                                            const QByteArray& talker)
{
    QByteArray body = R"({"available": true, "mode": "lsb", "sideband": "lsb",
    "low_hz": 100, "high_hz": 2900, "width_hz": 2800,
    "set_low_hz": 100, "set_high_hz": 2900,
    "shape": "soft", "taps": 511, "transition_hz": 122,
    "notches": [],
    "anf": {"enabled": false, "found_hz": [], "depth_db": []},
    "apf": {"enabled": false, "hz": 600.0, "width_hz": 80.0},
    "auto": {"enabled": false, "source": null, "low_hz": null, "high_hz": null},
    "auto_eq": {"enabled": false, "tilt_db": 0.0},
    "nb": {"enabled": false, "threshold_db": 8.0, "blanked_pct": 0.0},
    "agc": {"mode": "med", "attack_ms": 10, "decay_ms": 500, "hang_ms": 250,
            "threshold_db": 20.0, "gain_db": -1.9},
    "roofing": {"analogue_hz": 200000.0, "digital_hz": 25000.0}, "contour": )";
    body += contour;
    body += ", \"talker\": ";
    body += talker;
    body += ", ";
    body += makeFilterSpectrum();
    body += ", ";
    body += makeFilterResponse();
    body += "}";
    return body;
}

// AUTO CONTOUR fitting, and it has a print to fit to: a bell at 550 Hz.
inline const QByteArray kFilterContourFitted =
    R"({"enabled": true, "hz": 550.0, "db": -3.0, "width_hz": 500.0,
        "auto": true, "source": "print"})";
// AUTO CONTOUR fitting and nothing heard yet. hz and width_hz are NULL, which
// is not "0 Hz" and must never be drawn as one.
inline const QByteArray kFilterContourNoPrint =
    R"({"enabled": false, "hz": null, "db": 0.0, "width_hz": null,
        "auto": true, "source": null})";
// The operator's own bell: auto off, source "manual".
inline const QByteArray kFilterContourManual =
    R"({"enabled": true, "hz": 700.0, "db": -4.0, "width_hz": 400.0,
        "auto": false, "source": "manual"})";

// PER TALKER on, snapping, with #3 in force -- the id /diversity's memory
// names "Ted".
inline const QByteArray kFilterTalkerOnFast =
    R"({"enabled": true, "snap": "fast", "id": 3, "remembered": [3, 4, 7]})";
// PER TALKER on and gliding, with an id the gate has no name for.
inline const QByteArray kFilterTalkerOnSmooth =
    R"({"enabled": true, "snap": "smooth", "id": 4, "remembered": [3, 4, 7]})";
// PER TALKER off: one standing filter for everybody, and no id at all.
inline const QByteArray kFilterTalkerOff =
    R"({"enabled": false, "snap": "smooth", "id": null, "remembered": []})";

inline const QByteArray kDiversityFilterTalkerAuto =
    makeDiversityFilterTalker(kFilterContourFitted, kFilterTalkerOnFast);
inline const QByteArray kDiversityFilterTalkerNoPrint =
    makeDiversityFilterTalker(kFilterContourNoPrint, kFilterTalkerOnSmooth);
inline const QByteArray kDiversityFilterTalkerOff =
    makeDiversityFilterTalker(kFilterContourManual, kFilterTalkerOff);

} // namespace DiversityGateFixture
