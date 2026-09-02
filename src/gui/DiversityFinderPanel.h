#pragma once

// The BAND page's FINDER: "where is there a conversation worth listening to,
// and would the second loop buy me anything there?"
//
// The gate runs a syllabic-modulation detector over the last ten minutes of
// both loops' spectra and reports two things. This widget is the view of both:
//
//   * DiversityActivityStrip -- one row, aligned column-for-column with the
//     spatial waterfall above it, whose brightness is the fraction of those
//     ten minutes each bin carried voice. A net that meets every evening on
//     the same frequency is a bright bar; a single over is a faint one.
//
//   * DiversityFinderPanel -- the ranked candidate table. Each row is one
//     conversation the gate found, with the numbers that decide whether to go
//     there: the score it was ranked by, SNR, how voice-shaped the envelope
//     was, how long it has been active, how long ago it was last heard, the
//     pair's phase and coherence, and the diversity gain the two loops can
//     earn on it. Every row tunes -- from its button, from a double-click, or
//     from the keyboard through the button.
//
// Nothing here is invented. The gate contract marks every candidate field as
// optional (an older gate has fewer of them), and a field that did not arrive
// renders as an em dash rather than as a zero that looks like a measurement.
//
// It owns no transport and no timer: DiversityWindow feeds it one
// applyFinder() per /diversity/finder poll and every tune leaves as
// tuneRequested().

#include <QVector>
#include <QWidget>

class QJsonObject;
class QLabel;
class QPaintEvent;
class QTableWidget;

namespace AetherSDR {

// setActivity() is deliberately not one of check_a11y.py's watched setter
// names (setLevel/setDbm/setFrequency/update*): the strip is a glance-view of
// the same ten minutes the candidate table below it lists as text rows, so it
// carries no data a screen reader cannot already reach -- the "decorative vs.
// data-carrying" split docs/a11y.md draws, and the same exemption
// DiversityMapStrip takes.
class DiversityActivityStrip : public QWidget {
    Q_OBJECT
public:
    explicit DiversityActivityStrip(QWidget* parent = nullptr);

    // One /diversity/finder "activity" array, 0..1 per bin. An empty vector is
    // "nothing measured": the strip goes to bare background rather than to a
    // row of zero-height bars that would read as "measured, and silent".
    void setActivity(const QVector<float>& activity);

    int binCount() const { return int(m_activity.size()); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<float> m_activity;
};

class DiversityFinderPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiversityFinderPanel(QWidget* parent = nullptr);

    // One /diversity/finder answer. {"available": false}, an {"error": ...}
    // body and a route an older gate never had all empty the table and say so
    // in the header line -- an empty finder is a fact, and leaving last
    // minute's candidates up would be a lie about what is on the band now.
    void applyFinder(const QJsonObject& finder);

    // Gate gone, or diversity no longer available.
    void clear();

    QTableWidget* table() const { return m_table; }

signals:
    // A row's Tune button, or a double-click on the row. Carries the
    // candidate's own centre frequency in Hz.
    void tuneRequested(double hz);

private:
    // Rebuilds the table from one ranked candidate array. Order is the gate's:
    // "candidates" arrives best-first and is NOT re-sorted here, because the
    // rank is the gate's judgement and a table that re-sorted it would be
    // quietly disagreeing with the score column it is showing.
    void setCandidates(const QJsonObject& finder);
    void tuneRow(int row);

    DiversityActivityStrip* m_strip{nullptr};
    QLabel*                 m_stripCaption{nullptr};
    QLabel*                 m_caption{nullptr};
    QTableWidget*           m_table{nullptr};
    // Centre frequency per row, in Hz -- the tune target, kept out of the
    // displayed kHz cell so a rounded label can never become the frequency
    // actually written to the radio.
    QVector<double>         m_rowHz;
};

} // namespace AetherSDR
