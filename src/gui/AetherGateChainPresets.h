#pragma once

// PRESETS -- the whole chain as the operator left it, saved under a name.
//
// The operator's word (B21): "presets". The MODE row already has two of them
// and they are the app's, from a table in AetherGateChainModes.cpp: SET UP FOR
// PHONE is twelve writes, SET UP FOR CW is fourteen. What it did not have was
// a way to keep YOUR twelve -- the roof, the shape, the blanker, the AGC and
// the pair settings you arrived at at two in the morning on 40 m -- and get
// them back tomorrow night.
//
// So a preset here is EXACTLY a mode set whose values came from the receiver
// instead of from a table. It is applied by the same AetherGateChainPreset
// sequencer, one write at a time, each waited for, with the same settling
// window and the same refusals on the same tiles. Nothing about a preset is
// optimistic and nothing about it is a bulk route: the gate has no preset
// object and this does not pretend it does.
//
// WHAT IS IN ONE. Every stage the gate reports that can actually be written --
// every toggle and every select on the current chain -- and nothing else. A
// fixed row (the LNA, the sample rate, the antenna port) is not in a preset
// because a preset that claimed to restore it would be lying: those are set on
// the setup page and the chain window has never been able to move them. The
// two-loop rows are in one exactly when the gate sends them, for the same
// reason: POST-FILTER and SUB-BAND NULL are toggles on a dual-tuner chain and
// simply absent on a single-tuner one.
//
// WHERE IT LIVES. One JSON file per preset, under AppDataLocation +
// "/chain-presets/<slug>.json":
//
//   {"name": "Net night", "saved": "2026-09-03T04:12:07Z", "mode": "phone",
//    "stages": {"roof_digital": "3000", "shape": "soft", "nb": "on", ...}}
//
// One file per preset rather than one library file, because that IS the
// import and the export: a preset is a file you can mail, drop in the folder
// and pick from the menu. Written through QSaveFile, so a preset is never half
// a preset -- the same atomic write the rest of the app's JSON stores use.
//
// A stage the gate no longer has is SKIPPED on load, with its name said out
// loud in the window's refusal line. Not an error: an operator who saved a
// preset on the dual-tuner pair and loaded it on one tuner has not done
// anything wrong, and a load that failed whole because of POST-FILTER would be
// the app being pedantic about a receiver the operator can see in front of him.
//
// "EDITED" IS A COMPARISON, NOT A FLAG. Once a preset has been loaded, every
// /filter body the window receives is held against it: the moment any stage
// the preset names reads differently from what the preset says, the row says
// "in force: Net night (edited)"; put it back by hand and the word goes away
// again. That is the only honest definition -- a write the gate refused never
// changed anything, and a knob turned from another client did, and a flag set
// on the app's own writes would get both of those wrong.

#include "gui/AetherGateChainModes.h"
#include "gui/AetherGateChainStage.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace AetherSDR {

// One preset file, read or about to be written. `stages` is ORDERED: it is the
// gate's own signal order at the moment the preset was captured, which is also
// the order the writes go out in, which is why AUTO WIDTH lands before the
// edges it would otherwise move back.
struct ChainPresetDoc {
    QString name;
    QString saved;    // ISO-8601 in UTC, as the gate's own timestamps are
    QString mode;     // "phone" | "cw" | "data"
    QString path;     // where it was read from, or written to
    QList<QPair<QString, QString>> stages;   // stage id -> the value in force

    bool isNull() const { return name.isEmpty(); }
};

// "Net night / 80m" -> "net-night-80m". Lower case, anything that is not a
// letter or a digit becomes one hyphen, and a name with nothing left in it
// (say a name written entirely in a script this slug cannot transliterate)
// becomes "preset" rather than an empty file name.
QString chainPresetSlug(const QString& name);

// AppDataLocation + "/chain-presets", created if it is not there.
QString chainPresetDir();

// The rows as they stand, as a preset. Only stages that can be written are in
// it: see the header comment.
ChainPresetDoc chainPresetCapture(const QList<ChainStage>& rows, ChainMode mode,
                                  const QString& name);

// Writes `doc` and fills in doc.path. A slug already taken by a preset with a
// DIFFERENT name gets a numeric suffix; the same name overwrites itself, which
// is what "save again" means.
bool chainPresetSave(ChainPresetDoc& doc);

ChainPresetDoc chainPresetRead(const QString& path);

// Every preset on disk, by name.
QList<ChainPresetDoc> chainPresetAll();

// The preset, as the ordered list of writes the mode sets are made of. A stage
// the gate is not currently reporting is skipped and its NAME (or its id, when
// the app has never heard of it either) is appended to `missing`.
QList<ChainPresetWrite> chainPresetSet(const ChainPresetDoc& doc,
                                       const QList<ChainStage>& rows,
                                       QStringList* missing);

// The SETUP row: a menu, LOAD, SAVE AS..., DELETE, and the one-line notice a
// delete leaves behind. It sits on the MODE row, beside SET UP FOR <mode> --
// both are whole-chain actions and read as a pair (design §2.6). No modal
// dialogs anywhere in it -- SAVE AS... opens an inline field where the menu
// was, and DELETE is undoable for eight seconds instead of asking first. A
// confirmation box is a question nobody reads; an undo is an answer.
class AetherGateChainPresetBar : public QWidget {
    Q_OBJECT
public:
    explicit AetherGateChainPresetBar(QWidget* parent = nullptr);

    // What a save captures. The bar owns no chain of its own: the window hands
    // it the rows and the mode at the moment SAVE lands.
    void setSource(std::function<QList<ChainStage>()> rows,
                   std::function<ChainMode()> mode);

    // One /filter body's rows, held against the loaded preset. Called by the
    // window on every body that arrives while no preset is being applied.
    void noteRows(const QList<ChainStage>& rows);

    // The name in the menu right now, "" when none is chosen. Read back by the
    // tests, and by nothing else.
    QString currentName() const;
    // The preset in force -- the one last loaded or saved -- and whether the
    // receiver has since drifted from it.
    QString loadedName() const { return m_loaded.name; }
    bool    edited() const { return m_edited; }

signals:
    // Apply this preset: the ordered writes, the preset's name for the
    // progress line, and the stages the gate no longer has.
    void applyRequested(QList<ChainPresetWrite> writes, QString name,
                        QStringList missing);

private:
    void reload(const QString& keepName = QString());
    void beginSaveAs();
    void endSaveAs(bool commit);
    void doLoad();
    void doDelete();
    void showNotice(const QString& text);
    // The "in force" line and the menu item's "(edited)" suffix, from
    // m_loaded and m_edited.
    void refreshState();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QComboBox*   m_pick{nullptr};
    QPushButton* m_load{nullptr};
    QPushButton* m_save{nullptr};
    QPushButton* m_delete{nullptr};
    QLineEdit*   m_name{nullptr};
    QLabel*      m_state{nullptr};
    QPushButton* m_notice{nullptr};
    QTimer*      m_noticeTimer{nullptr};
    // The preset DELETE took away, kept whole so UNDO can put the same file
    // back rather than a reconstruction of it.
    ChainPresetDoc m_deleted;
    // The preset in force, whole, because "edited" is a comparison against it.
    ChainPresetDoc m_loaded;
    bool           m_edited{false};
    std::function<QList<ChainStage>()> m_rows;
    std::function<ChainMode()>         m_mode;
};

} // namespace AetherSDR
