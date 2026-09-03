#pragma once

// DiversityFilterControls -- the whole body of the Diversity window's FILTER
// page.
//
// This page used to be the gate's whole slice filter chain drawn and driven:
// a response curve, a PER TALKER strip, and four columns of controls --
// roofing, blanker, shape, notch, the automatic notcher, contour, the audio
// peaking filter, auto EQ, AGC. None of that is specific to having a SECOND
// antenna -- every one of those exists for a single receiver, and the gate's
// own CHAIN window (AetherGateChainWindow) now draws the same chain once,
// rather than once per place a receiver's filter shows up. This page keeps
// only what a receiver alone could never have: the coherence post-filter and
// the sub-band MRC weighting, the two stages that only exist because the
// combiner is adding a second loop to the first, plus the one button that
// opens the CHAIN window for everything else.
//
// Unlike the old version of this page, nothing here is polled off /filter and
// nothing needs a write hold. POST-FILTER and MRC are /diversity/set keys,
// read back off the same status object MODE/HEAR/PAN already use
// (DiversityWindowChain.cpp) at that poll's ordinary rate -- and that group's
// own convention, which this class follows rather than inventing a second
// one, is a plain click and a check-back guarded by a QSignalBlocker, because
// nothing on it has ever needed more.

#include <QString>
#include <QUrlQuery>
#include <QWidget>

class QButtonGroup;
class QJsonObject;
class QLabel;
class QPushButton;

namespace AetherSDR {

class DiversityFilterControls : public QWidget {
    Q_OBJECT
public:
    explicit DiversityFilterControls(QWidget* parent = nullptr);

    // /diversity's "post" object: {"enabled": bool, "version": 1|2, ...}, v2
    // adding "snr_in_db"/"snr_out_db"/"pause_fraction"/"hold". Checks the
    // OFF/V1/V2 group back and fills the readout; a v1 gate (or a v2 one that
    // has not measured anything yet) reads out as "v1" rather than a dash
    // that would look like a failed read.
    void applyPost(const QJsonObject& post);

    // /diversity's "mrc" object: {"enabled": bool, "gain_over_broadband_db"?,
    // "bins_used"?}. Older gates omit both optional fields; the readout is a
    // dash then rather than a number nothing measured.
    void applyMrc(const QJsonObject& mrc);

    // Gate gone. Every group unchecked, every readout back to a dash.
    void clear();

signals:
    // The OPEN CHAIN button. Wired to AetherGateApplet::toggleChainWindow by
    // whoever owns that connection (AetherGateApplet.cpp) -- this page only
    // asks; it has no idea the chain window exists.
    void requestOpenChain();

    // -> GET /diversity/set?<query>, the same route and the same signal shape
    // DiversityWindow's own MODE/HEAR/PAN buttons use. Routed through
    // DiversityWindow rather than emitted there directly because those groups
    // live in DiversityWindow itself and this is a separate widget.
    void requestSet(QUrlQuery query);

private:
    QWidget* buildPairStagesBox();
    // Checks the button carrying `value` in its "filterValue" property, with a
    // QSignalBlocker so the check-back does not turn into another write --
    // exactly DiversityWindow::checkValue()'s own rule, by hand, because that
    // one is a private member of a different class.
    static void checkValue(QButtonGroup* group, const QString& value);

    QPushButton* m_openChainButton{nullptr};
    QLabel*      m_movedLabel{nullptr};

    // --- POST-FILTER -------------------------------------------------------
    QButtonGroup* m_postGroup{nullptr};
    QLabel*       m_postReadout{nullptr};

    // --- SUB-BAND MRC --------------------------------------------------------
    QPushButton* m_mrcButton{nullptr};
    QLabel*      m_mrcReadout{nullptr};
};

} // namespace AetherSDR
