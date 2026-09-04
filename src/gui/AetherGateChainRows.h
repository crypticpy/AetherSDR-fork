#pragma once

// /filter, turned into rows.
//
// Two functions and nothing else, split out of AetherGateChainStrip.{h,cpp}
// so the strip can be about LAYOUT and this can be about the contract:
//
//   * chainFromFilter() -- the gate's own `chain` array when there is one.
//     Rows the app has never heard of render from their own name, detail and
//     action, so a gate that grows a stage does not need an app release. That
//     is the entire point of the array being gate-authored.
//
//   * a built-in 13-row FALLBACK for a gate with no `chain` key. Assembled
//     from the flat low_hz/high_hz/shape/notches/anf/contour/apf/auto/auto_eq/
//     nb/agc/talker/roofing keys only, so the window is useful against an
//     older gate rather than only against the current one. Rows the flat keys
//     cannot answer -- ALIGN, COMBINER, SUB-BAND, POST-FILTER, all of which
//     live in /diversity -- are deliberately absent rather than invented.
//
// Nothing here sorts or reorders. The gate's order IS signal order, and a
// window that rearranged the stages would be describing a receiver nobody
// owns. What the app does do is GROUP them into four columns for reading;
// that table is chainStageGroup() in AetherGateChainModes.h.
//
// A third source joins the two above: GET /device's own "frontend" key,
// which is the linearity guard (B23) rather than anything /filter's chain[]
// carries. It is parsed by chainFrontendFromDevice() and turned into two
// synthetic FrontEnd-group rows (HEADROOM, GUARD) by chainFrontendRows() --
// synthetic because no gate authors them the way it authors chain[], but
// they are still ChainStage values and draw through the exact same tile and
// control code every other row does.

#include "gui/AetherGateChainStage.h"

#include <QList>
#include <QtGlobal>

class QJsonObject;

namespace AetherSDR {

// The rows one /filter answer describes. `fromGate` (optional) reports
// whether they came from the gate's own chain[] or from the built-in
// fallback. `autoCleanOut` (optional) receives the auto_clean row when the
// gate's own chain[] carries one, or a default-constructed ChainStage (empty
// id) when it does not -- AUTO CLEAN never rejoins the returned list; the
// CHAIN window's NOW strip (AetherGateChainNow.h) is the only thing that
// still draws it. Ignored entirely on the fallback path (chainFallback()
// never carries an auto_clean row at all).
QList<ChainStage> chainFromFilter(const QJsonObject& filter, bool* fromGate = nullptr,
                                  ChainStage* autoCleanOut = nullptr);

// The 13 rows a chain-less /filter can honestly describe, exported for the
// test that pins them.
QList<ChainStage> chainFallback(const QJsonObject& filter);

// "12 kHz", "2.8 kHz", "300 Hz", "1.536 MHz". The unit follows the number
// rather than a fixed choice, because a roofing menu that reads
// "0.3 kHz / 0.6 kHz / 1.2 kHz" is not the menu on anybody's front panel.
QString chainFormatWidth(double hz);

// --------------------------------------------------------------------------
// B23 -- the front-end linearity guard, from GET /device's "frontend" key
// --------------------------------------------------------------------------

// One line of the guard's own history: "stepped 0 -> 1 at 11:42, clipping".
struct ChainFrontendEvent {
    qint64  t{0};
    QString from;
    QString to;
    QString reason;
};

// The whole of "frontend", transcribed rather than interpreted -- the same
// contract chain[] rows keep. `available` false means every other field is
// default-constructed and nothing built from this should be shown.
struct ChainFrontendStatus {
    bool    available{false};
    bool    guard{false};
    QString floorState;
    QString maxState;
    QString lnaState;
    bool    dbmCalibrated{true};
    QString calState;
    bool    hasHeadroom{false};
    double  headroomDb{0.0};
    int     clips1s{0};
    QString state;   // idle | stepping_up | holding | stepping_down
    QList<ChainFrontendEvent> events;
};

// device.value("frontend"). Missing, not an object, or "available": false
// all come back with `available` false.
ChainFrontendStatus chainFrontendFromDevice(const QJsonObject& device);

// HEADROOM and GUARD, in that order -- empty when the guard is not
// available, because a summary card with one row hanging off the end of
// seven others is worse than a card that is honestly seven rows short.
QList<ChainStage> chainFrontendRows(const ChainFrontendStatus& fe);

// The GUARD row's value sentence: "on · LNA 1 (floor 0) · stepping up",
// "on · LNA 0 (floor 0)", "off · LNA 0". Shared by the row's tooltip and the
// inspector, so the two can never disagree about what the guard is doing.
QString chainFrontendGuardValueText(const ChainFrontendStatus& fe);

// The last entry of `events` as one sentence, or an empty string when there
// have been none yet -- the inspector falls back to the row's own detail in
// that case rather than printing nothing.
QString chainFrontendEventSentence(const ChainFrontendStatus& fe);

// The calibration caveat -- empty when `dbmCalibrated` is true. Shared by
// the FRONT END card's one-line note and the GUARD row's inspector caveat,
// so the two sentences the operator can see about this are the same one.
QString chainFrontendCalNoteText(const ChainFrontendStatus& fe);

} // namespace AetherSDR
