#pragma once

// Pieces DiversityWindow builds its stage panels out of, split from
// DiversityWindow.cpp so that file stays inside the size budget AGENTS.md
// asks for (the same extraction AetherGateDiversityFormat.cpp is for
// AetherGateDiversityPanel.cpp).
//
// Two things live here:
//
//   * DiversitySnrMeter -- a vertical signal-to-noise meter on a FIXED
//     -10..+30 dB scale. Deliberately NOT ClientLevelMeter: that widget's
//     scale is hard-wired to -60..0 dBFS audio peak with fast-attack /
//     slow-release ballistics, so every SNR at or above 0 dB would peg it at
//     full scale and read as "the same" (Principle XI -- a meter that cannot
//     distinguish +2 dB from +28 dB is claiming a measurement it does not
//     have). It keeps ClientLevelMeter's painting conventions -- antialiasing
//     off for bars and ticks, text antialiasing on, dB ticks as a static
//     constexpr table, the shared color.meter.bar.fillGradient brush -- so it
//     reads as the same family of instrument.
//
//   * DiversityWidgets -- free helpers shared by the sidebar panel and the
//     window: the group-box frame both use, the small themed labels the
//     window's panels are built from, and the one implementation of "fill a
//     QListWidget from /diversity's sources array" that they now share rather
//     than keeping two copies of its rebuild-only-on-change and
//     restore-selection-by-(lo,hi)-key rules in step by hand.
//
// It also carries two of DiversityWindow's own panel builders
// (buildStationsPanel/buildAlignmentPanel). They are members of
// DiversityWindow, defined here purely so DiversityWindow.cpp stays inside
// the file-size budget -- the same reason this file exists at all.

#include <QString>
#include <QWidget>

#include <limits>

class QJsonArray;
class QFrame;
class QLabel;
class QListWidget;
class QVBoxLayout;

namespace AetherSDR {

class DiversitySnrMeter : public QWidget {
    Q_OBJECT
public:
    // `header` is the leg this meter shows -- "A", "B" or "OUT". It is also
    // the basis of the accessible name, so it is a caption, not decoration.
    explicit DiversitySnrMeter(const QString& header, QWidget* parent = nullptr);

    // One leg of /diversity's snr_db. A null leg (the gate has no estimate
    // yet) is not 0 dB: pass valid == false and the bar empties and the
    // readout shows a dash rather than a number nothing measured.
    void setSnrDb(double db, bool valid);

    // Empties the meter -- gate gone, or diversity no longer available.
    void clearReading();

    // Last value shown, or NaN when the meter is showing "no reading".
    // Painted widgets have no child to read back from; tests have no other
    // way to check what the meter would display.
    double shownDb() const
    {
        return m_valid ? m_db : std::numeric_limits<double>::quiet_NaN();
    }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_header;
    double  m_db{0.0};
    bool    m_valid{false};
};

namespace DiversityWidgets {

// One stage panel of the window: a QFrame carrying the objectName
// "stripGroupBox" that the Aetherial Audio channel strip's own stylesheet
// rule targets, a small all-caps caption, and a body layout the caller fills.
// `body` is set to that layout.
QFrame* makeGroupBox(const QString& caption, const QString& objectName,
                     QVBoxLayout*& body, QWidget* parent);

// A small all-caps caption over a group of controls ("MODE", "HEAR", "PAN").
QLabel* makeCaption(const QString& text, QWidget* parent);

// A field caption beside a readout ("Lag", "Corr peak") -- and, for the one
// status line that is a sentence rather than a number, the sentence itself.
QLabel* makeFieldLabel(const QString& text, QWidget* parent);

// A numeric readout whose MINIMUM width is the widest string it can ever
// show, so the row it sits in cannot reflow when the digit count changes --
// the fixed-frame rule the whole window is built to.
QLabel* makeValue(const QString& objectName, const QString& worstCase, QWidget* parent);

// Flips a widget's "live" property and re-polishes it so a [live="..."] rule
// in its style sheet takes effect. The alternative -- a setStyleSheet() per
// poll -- reparses the sheet and drops the widget's cached style every
// second.
void setLive(QWidget* w, bool live);

// Fills `list` from one /diversity "sources" array.
//
// Rebuilds only when the built row strings (or their tooltips) actually
// differ from what is already there, so scroll position and an untouched
// selection survive a poll that reports the identical array back. When a
// rebuild IS needed, the previously selected item is re-found by its own
// (lo_hz, hi_hz) rather than by its old row: "sources" is gate-ordered and
// can shrink or reorder between polls, and restoring by raw row number would
// silently point a "Null selected" button at a DIFFERENT source than the one
// the operator picked.
void applySources(QListWidget* list, const QJsonArray& sources);

} // namespace DiversityWidgets

} // namespace AetherSDR
