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
// THE GRID. A bearing needs two points. The gate knows where every beacon is
// and can find out where YOU are in exactly one way: you tell it, once, as a
// Maidenhead locator. So the first line of this panel is that field. Without it
// the Brg and km columns are dashes and the pattern plot says so rather than
// drawing a dial with nothing on it -- four characters of typing is a cheaper
// fix than a picture that pretends to be a measurement.
//
// PROPAGATION. Results survive a gate restart now, so the log is a night's work
// rather than a snapshot, and eighteen rows of one band is no longer the whole
// of it. The per-band lines under the grid row are the rest, collapsed: how
// many of the eighteen were heard on each band the gate has sampled, the lowest
// power step that made it, the median signal-to-noise, and how stale the answer
// is. The table stays on the band you are tuned to, because a row from 20 m
// would be a lie about 15 m.
//
// BEACON CHECK. The five beacon frequencies are the only place in the hobby
// where "go and listen for three minutes" is a complete measurement procedure.
// The CHECK buttons are that procedure: tune the active slice to a band's
// beacon frequency, leave it there for one full eighteen-slot cycle plus slack,
// then put the radio back exactly where it was. It moves the frequency and
// NOTHING else -- not the mode, not the combiner, not the filter -- because a
// check that changed the receiver would not be measuring the receiver you use.
//
// Results for other bands stay in memory but are not drawn in the table: the
// gate reports per (band, call) and the schedule frequency changes when you
// retune. Nothing here is invented; a beacon that has not been heard on this
// band is dashes, not zeros.
//
// It owns no transport: DiversityWindow feeds it one applyBeacons() per
// /diversity/beacons poll, which the applet's DiversityBandPoller makes only
// while the SITE page is on screen, and its two writes leave as
// actionRequested() and tuneRequested().

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrlQuery>
#include <QWidget>

class QJsonArray;
class QJsonValue;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace AetherSDR {

class DiversityBeaconPattern;

class DiversityBeaconPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiversityBeaconPanel(QWidget* parent = nullptr);

    // One /diversity/beacons answer. {"available": false}, an {"error": ...}
    // body and a route an older gate never had all say so in the header line
    // and dash the table -- an idle beacon watch is a fact about the frequency
    // you are on, not a reason to leave last band's log up.
    void applyBeacons(const QJsonObject& beacons);

    // The reply to this panel's own SET/FORGET. Only an {"error": ...} body
    // means anything here: the locator itself comes back on the next beacons
    // poll, which is the gate's answer rather than our optimism. Ignored unless
    // this panel is the one that asked.
    void applyActionReply(const QJsonObject& reply);

    // Where the radio is tuned right now, in absolute hertz, pushed down from
    // AetherGateApplet -- the only object in the section that can see the
    // SliceModel. It is what a BEACON CHECK remembers before it leaves, and
    // without it a check would have nowhere to come back to and so does not
    // start at all.
    void setActiveSliceHz(double hz);

    // Ends a running check and tunes back at once. Called by CANCEL and by the
    // window closing: a countdown nobody can see must not still be holding the
    // radio on a beacon frequency ten minutes later.
    void cancelCheck();
    // True while a check or sweep is out, and for a few seconds after it
    // comes home: the last slot is scored at the slot boundary, so the poll
    // that carries it lands after the countdown ends. AetherGateDiversityPanel
    // keeps the /diversity/beacons poll running on this whatever page is up.
    bool pollWanted() const;

    // Gate gone, or diversity no longer available. Forgets the results too:
    // they belong to a gate session, and a reconnect may be to a different
    // radio at the same address.
    void clear();

    // The window's own station note rides on the locator's row. The locator
    // and the note are the two things the operator TELLS the gate about this
    // station -- one it can check, one it cannot -- and they are only useful
    // read together. It arrives as a finished widget rather than as a line of
    // this panel because the note's write and its check-back are the window's
    // (it comes back on /diversity, which this panel never sees), and because
    // the SITE page has no spare height for a second row.
    void setGridRowExtra(QWidget* extra);

    QTableWidget* table() const { return m_table; }
    DiversityBeaconPattern* pattern() const { return m_pattern; }

public slots:
    // One second of a running BEACON CHECK. A slot rather than a lambda on the
    // timer so a test can step the countdown directly instead of waiting out
    // three real minutes.
    void checkTick();

signals:
    // -> GET <route>?<query> on the gate: /diversity/set?grid=... today. Same
    // door the noise profile's action buttons use.
    void actionRequested(QString route, QUrlQuery query);
    // -> the ACTIVE SLICE, in absolute hertz. Both legs of a BEACON CHECK leave
    // by here: the trip out to the beacon frequency and the trip home.
    void tuneRequested(double hz);
    // A check or sweep started, moved to its next band, or came home:
    // re-evaluate the site poll (pollWanted()).
    void checkStateChanged();

private:
    QWidget* buildGridRow();
    QWidget* buildCheckRow();
    // The dial and the per-band summary beside the schedule table.
    QWidget* buildPatternColumn();
    // The one line that carries a refusal, and the two timers that expire it
    // and step the check countdown.
    QWidget* buildStatusLine();
    // Redraws all eighteen rows from m_results for the current band.
    void renderRows();
    // One line per band the gate has sampled, from the "propagation" array.
    void renderPropagation(const QJsonValue& propagation);
    // CHECK <band>: one band, then home. SWEEP ALL: the five in a row, then
    // home, with one report for the lot. beginCheck() is the leg out that
    // both share; finishCheck() is the countdown reaching zero (next band or
    // home); endRun() is the leg home, which CANCEL takes too.
    void startCheck(int bandIndex);
    void startSweep();
    void beginCheck(int bandIndex);
    void finishCheck();
    void endRun();
    void updateCheckLabel();
    // The run's result, one line per band swept, from the results the gate
    // reported since the run started; and what those results feed.
    void renderReport();
    void renderFeeds(const QJsonObject& beacons);
    void showTransient(const QString& text);

    QLabel*       m_header{nullptr};
    QLabel*       m_caption{nullptr};
    QLabel*       m_status{nullptr};
    QTimer*       m_statusTimer{nullptr};
    QTableWidget* m_table{nullptr};
    QLineEdit*    m_gridEdit{nullptr};
    QPushButton*  m_gridSetButton{nullptr};
    QPushButton*  m_gridForgetButton{nullptr};
    QLabel*       m_gridHint{nullptr};
    QHBoxLayout*  m_gridRowLayout{nullptr};
    QLabel*       m_propagation{nullptr};
    QLabel*       m_checkLine{nullptr};
    QPushButton*  m_checkCancelButton{nullptr};
    QPushButton*  m_sweepButton{nullptr};
    QLabel*       m_feedsLine{nullptr};
    DiversityBeaconPattern* m_pattern{nullptr};

    // Keyed "<band_hz>|<call>" -- the gate reports per (band, call) and a
    // result is only ever about the band it was heard on.
    QHash<QString, QJsonObject> m_results;
    double  m_bandHz{0.0};
    QString m_nowCall;
    // The gate's own answer, or empty when it has none. The edit box shows it
    // whenever the operator is not typing into it.
    QString m_stationGrid;
    bool    m_actionPending{false};

    // --- BEACON CHECK -----------------------------------------------------
    QTimer* m_checkTimer{nullptr};
    // Where the radio was when the check started, in hertz, and where it went.
    double  m_checkReturnHz{0.0};
    int     m_checkBand{-1};
    int     m_checkLeftS{0};
    double  m_activeSliceHz{0.0};
    // The bands still to do in a sweep, the ones done in this run (the
    // report's rows), when the run left (results older than that are another
    // run's), and how long after coming home the poll stays wanted.
    QList<int> m_sweepQueue;
    QList<int> m_swept;
    double  m_runStartedAt{0.0};
    qint64  m_settleUntilMs{0};
    QTimer* m_settleTimer{nullptr};
};

} // namespace AetherSDR
