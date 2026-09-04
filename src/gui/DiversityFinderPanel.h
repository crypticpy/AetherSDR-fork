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
//     the same frequency is a bright bar; a single over is a faint one. Where
//     the gate has named what it heard, the bar takes that kind's colour, so
//     the CW end of the band and the phone end read apart at a glance.
//
//   * DiversityFinderPanel -- the ranked candidate table. Each row is one
//     conversation the gate found, with the numbers that decide whether to go
//     there: what the gate thinks it is and how sure it is of that, the score
//     it was ranked by, SNR, how voice-shaped the envelope was, how long it
//     has been active, how long ago it was last heard, the pair's phase and
//     coherence, and the diversity gain the two loops can earn on it. Every
//     row tunes -- from its button, from a double-click, or from the keyboard
//     through the button.
//
// Nothing here is invented. The gate contract marks every candidate field as
// optional (an older gate has fewer of them), and a field that did not arrive
// renders as an em dash rather than as a zero that looks like a measurement.
//
// It owns no transport and no timer: DiversityWindow feeds it one
// applyFinder() per /diversity/finder poll and every tune leaves as
// tuneRequested().

#include <QString>
#include <QVector>
#include <QWidget>

class QJsonObject;
class QLabel;
class QPaintEvent;
class QTableWidget;

namespace AetherSDR {

// One candidate's stretch of the strip, and the gate's word for what is in
// it: "voice", "cw", "data" (or "rtty"/"ft8"/"ft4"/"psk31" by name),
// "carrier", "noise", or "signal" for a verdict the gate would rather not
// narrow further. A gate that sends no kinds at all sends no bands.
struct DiversityKindBand {
    float   from{0.0f};      // 0..1 across the span the strip is drawn over
    float   to{0.0f};
    QString kind;
};

// setActivity() and setKindBands() are deliberately not among check_a11y.py's
// watched setter names (setLevel/setDbm/setFrequency/update*): the strip is a
// glance-view of the same ten minutes the candidate table below it lists as
// text rows -- including the kind, which the table spells out in words in its
// own column -- so it carries no data a screen reader cannot already reach.
// That is the "decorative vs. data-carrying" split docs/a11y.md draws, and the
// same exemption DiversityMapStrip takes. Nothing here is colour-only.
class DiversityActivityStrip : public QWidget {
    Q_OBJECT
public:
    explicit DiversityActivityStrip(QWidget* parent = nullptr);

    // One /diversity/finder "activity" array, 0..1 per bin. An empty vector is
    // "nothing measured": the strip goes to bare background rather than to a
    // row of zero-height bars that would read as "measured, and silent".
    void setActivity(const QVector<float>& activity);

    // Where the named candidates are. The brightness still carries the
    // activity: the kind only chooses the hue, so a faint CW bar stays faint.
    void setKindBands(const QVector<DiversityKindBand>& bands);

    int binCount() const { return int(m_activity.size()); }
    int bandCount() const { return int(m_bands.size()); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<float>              m_activity;
    QVector<DiversityKindBand>  m_bands;
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
    static QString legendText();
    void showReason(const QString& reason);

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
    // Repaints the kind cells from the current theme's tokens. The cell
    // colours are set in code rather than by the table's stylesheet, so a
    // live theme switch has to be told about them.
    void applyKindColours();
    void tuneRow(int row);

    DiversityActivityStrip* m_strip{nullptr};
    QLabel*                 m_stripCaption{nullptr};
    QLabel*                 m_caption{nullptr};
    QTableWidget*           m_table{nullptr};
    // Centre frequency per row, in Hz -- the tune target, kept out of the
    // displayed kHz cell so a rounded label can never become the frequency
    // actually written to the radio.
    QVector<double>         m_rowHz;
    // The gate's kind word per row, kept so a theme switch can re-colour the
    // cells without waiting for the next poll.
    QVector<QString>        m_rowKind;
};

} // namespace AetherSDR
