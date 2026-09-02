#pragma once

// The SITE page's BEACONS: the one thing on a receiver that measures the
// station rather than the band.
//
// The NCDXF/IARU International Beacon Project runs eighteen transmitters spread
// around the world on a shared schedule. Every three minutes each one keys the
// same five frequencies in turn: its callsign, then four one-second dashes at
// 100, 10, 1 and 0.1 watts. Everything about that is a gift to somebody trying
// to answer "is my antenna any good?":
//
//   * The transmitters are known, fixed and identical, so a difference between
//     two nights is YOUR difference.
//   * The power steps are a calibrated ladder. The lowest step you can still
//     hear is the band's real reach in decibels, not an impression of it.
//   * They come from known directions, so the phase the two loops measure on a
//     beacon is a phase with a known answer -- which is exactly the data a
//     geometry solve wants and cannot get from an unknown station.
//
// The gate does the hearing (it knows the schedule from UTC and correlates the
// dashes); this panel is the log. Eighteen rows in SCHEDULE order -- the order
// they transmit in, which is the order they will next appear in -- rather than
// in the gate's own results order, because the operator's question at any
// moment is "who is on now and who is next", and a table that re-sorted itself
// by signal strength could not answer it.
//
// Results for other bands stay in memory but are not drawn: the gate reports
// per (band, call) and the schedule frequency changes when you retune, but a
// row from 20 m would be a lie about 15 m. Nothing here is invented; a beacon
// that has not been heard on this band is dashes, not zeros.
//
// It owns no transport and no timer: DiversityWindow feeds it one
// applyBeacons() per /diversity/beacons poll, which the applet's
// DiversityBandPoller makes only while the SITE page is on screen.

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QTableWidget;

namespace AetherSDR {

class DiversityBeaconPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiversityBeaconPanel(QWidget* parent = nullptr);

    // One /diversity/beacons answer. {"available": false}, an {"error": ...}
    // body and a route an older gate never had all say so in the header line
    // and dash the table -- an idle beacon watch is a fact about the frequency
    // you are on, not a reason to leave last band's log up.
    void applyBeacons(const QJsonObject& beacons);

    // Gate gone, or diversity no longer available. Forgets the results too:
    // they belong to a gate session, and a reconnect may be to a different
    // radio at the same address.
    void clear();

    QTableWidget* table() const { return m_table; }

private:
    // Redraws all eighteen rows from m_results for the current band.
    void renderRows();

    QLabel*       m_header{nullptr};
    QLabel*       m_caption{nullptr};
    QTableWidget* m_table{nullptr};

    // Keyed "<band_hz>|<call>" -- the gate reports per (band, call) and a
    // result is only ever about the band it was heard on.
    QHash<QString, QJsonObject> m_results;
    double  m_bandHz{0.0};
    QString m_nowCall;
};

} // namespace AetherSDR
