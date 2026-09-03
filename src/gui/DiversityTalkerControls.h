#pragma once

// DiversityTalkerControls -- the FILTER page's PER TALKER strip.
//
// The gate remembers a filter per talker, not one filter for the receiver. Turn
// PER TALKER on and the filter a station was last heard through comes back the
// block they key up: their edges, their shape, their AGC threshold, their
// contour. It is the same recall the combiner already does for the weight, one
// layer further down the chain, and it is the difference between a net where
// every station needs the tone control moved and one where they do not.
//
// Two controls, and they are two different questions:
//
//   * PER TALKER  -- is the recall on at all (/filter/set?talker=on|off).
//   * FAST/SMOOTH -- what happens at the seam (/filter/set?talker_snap=).
//                    FAST puts the new filter in on the block boundary; SMOOTH
//                    glides to it over about a second. FAST is right when the
//                    two filters are very different and you want the first
//                    syllable already correct; SMOOTH is right on a round-table
//                    where the change would otherwise be audible as a click.
//
// A widget of its own rather than two more members on DiversityFilterControls
// for the file-size reason that class is already split across two files -- and
// then a better one: the state line's "filter: Ted's (#3)" clause needs a name
// that is NOT in the /filter payload. The id comes from /filter's talker
// object, the name from /diversity's memory[], and this class is the one place
// the two are joined. Keeping that join here means DiversityFilterControls
// still holds no window state of its own.
//
// Nothing here is set optimistically: a click writes, and what the tick then
// shows is the gate's answer to that write.

#include <QHash>
#include <QString>
#include <QUrlQuery>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QJsonArray;
class QJsonValue;

namespace AetherSDR {

class DiversityTalkerControls : public QWidget {
    Q_OBJECT
public:
    explicit DiversityTalkerControls(QWidget* parent = nullptr);

    // /filter's "talker": {enabled, snap, id, remembered}. An absent or null
    // value is an older gate that has no per-talker filter at all -- the
    // controls go dead rather than showing an "off" the gate never said.
    void applyTalker(const QJsonValue& talker);

    // /diversity's memory[], for the name behind an id. Fed separately because
    // the two routes are polled separately; a name that has not arrived yet
    // renders as the number, which is what the TALKERS table does too.
    void setTalkerNames(const QJsonArray& memory);

    // Gate gone, or no filter for this mode.
    void clear();

    // "filter: Ted's (#3)" for the FILTER page's state line, or an empty
    // string when the recall is off or the gate names no talker -- an empty
    // clause is dropped by the caller rather than rendered as a dash, because
    // "no talker filter in force" is the ordinary state and not a missing
    // measurement.
    QString stateClause() const;

    // Every interactive child, so the FILTER page's own setControlsEnabled()
    // greys this strip with the rest of the page in one call.
    const QVector<QWidget*>& controls() const { return m_controls; }

signals:
    // -> GET <path>?<query>, routed exactly like every other write the FILTER
    // page makes. See DiversityFilterControls::requestFilter.
    void requestFilter(QString path, QUrlQuery query);

private:
    void set(const QString& key, const QString& value);

    QCheckBox*    m_check{nullptr};
    QButtonGroup* m_snapGroup{nullptr};

    QVector<QWidget*> m_controls;

    // Last /diversity memory[], id -> operator label. Only ids the gate gave a
    // string name for are in here: a null name is not an empty name.
    QHash<int, QString> m_names;

    // Last /filter talker object, reduced to what the state line needs.
    bool m_enabled{false};
    bool m_haveId{false};
    int  m_id{0};
};

} // namespace AetherSDR
