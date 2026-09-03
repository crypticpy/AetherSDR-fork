#include "gui/AetherGateChainVisual.h"

#include "core/ThemeManager.h"
#include "gui/DiversityFilterPanel.h"
#include "gui/DiversityWindowPanels.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

namespace AetherSDR {

namespace {

// The one readout under the picture, at its widest. Every field has a fixed
// format so the line cannot reflow when a digit appears: five figures of Hz,
// one decimal of dB, two figures of notches.
QString visualReadoutWorstCase()
{
    return QStringLiteral("20000-20000 Hz · floor -100.0 dB · 12 notches"
                          " · AUTO 20000-20000");
}

// The corner readout, at its widest.
QString cursorWorstCase()
{
    return QStringLiteral("20 000 Hz · -100.0 dB");
}

// "1450" -> "1 450". A thousands space and no comma: this is a FREQUENCY, and
// every other frequency in this application is grouped the same way.
QString groupedHz(qint64 hz)
{
    QString digits = QString::number(std::abs(hz));
    for (int at = digits.size() - 3; at > 0; at -= 3)
        digits.insert(at, QLatin1Char(' '));
    return hz < 0 ? QLatin1Char('-') + digits : digits;
}

const char* kCursorStyle =
    "QLabel { color: {{color.accent.bright}}; font-size: 10px;"
    " background: transparent; }";

} // namespace

AetherGateChainVisual::AetherGateChainVisual(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("gateChainVisual"));
    setAccessibleName(tr("The filter as a picture"));
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(0, 6, 0, 0);
    box->setSpacing(6);

    auto* caption = DiversityWidgets::makeCaption(tr("PASSBAND"), this);
    caption->setObjectName(QStringLiteral("gateChainVisualCaption"));
    caption->setToolTip(tr("What the filter does to everything arriving, drawn "
                           "over what is actually arriving. Drag an edge to "
                           "move it, double-click to notch what is under the "
                           "pointer, drag a notch mark to move it, right-click "
                           "one to take it away, click any mark to go to its "
                           "stage on the CHAIN tab."));
    caption->setAccessibleDescription(caption->toolTip());
    box->addWidget(caption);

    m_panel = new DiversityFilterPanel(this);
    m_panel->setMinimumHeight(320);
    m_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    box->addWidget(m_panel, 1);

    // The corner readout, IN the picture -- a child of the panel, laid out in
    // its top right corner. It is what makes a double-click notch trustworthy:
    // you can read the frequency you are about to kill before you kill it.
    m_cursor = new QLabel(m_panel);
    m_cursor->setObjectName(QStringLiteral("gateChainVisualCursor"));
    m_cursor->setAccessibleName(tr("Under the pointer"));
    m_cursor->setWordWrap(false);
    m_cursor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // The label sits IN the picture, so it must not eat the double-click that
    // is meant to drop a notch under it.
    m_cursor->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_cursor->setFixedWidth(
        m_cursor->fontMetrics().horizontalAdvance(cursorWorstCase()) + 4);
    ThemeManager::instance().applyStyleSheet(m_cursor,
                                             QString::fromLatin1(kCursorStyle));
    auto* corner = new QHBoxLayout(m_panel);
    corner->setContentsMargins(0, 4, 10, 0);
    corner->addStretch(1);
    corner->addWidget(m_cursor, 0, Qt::AlignTop | Qt::AlignRight);

    m_readout = DiversityWidgets::makeReadoutLine(
        QStringLiteral("gateChainVisualReadout"), visualReadoutWorstCase(),
        tr("The passband in force, the noise floor the receiver measured, how "
           "many notches are set, and where the automatic width has put the "
           "edges when it is running."),
        this);
    m_readout->setAccessibleName(tr("The filter now"));
    box->addWidget(m_readout);

    connect(m_panel, &DiversityFilterPanel::edgesDragged, this,
            &AetherGateChainVisual::onEdgesDragged);
    connect(m_panel, &DiversityFilterPanel::notchRequested, this, [this](double hz) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("add"), QString::number(qint64(hz)));
        emit requestWrite(QStringLiteral("/filter/notch"), q);
    });
    connect(m_panel, &DiversityFilterPanel::notchRemoveRequested, this,
            [this](double hz) {
                // The gate's own parameter for one notch: ?clear=<hz> takes
                // that one away, ?clear=1 takes them all.
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("clear"), QString::number(qint64(hz)));
                emit requestWrite(QStringLiteral("/filter/notch"), q);
            });
    connect(m_panel, &DiversityFilterPanel::notchMoveRequested, this,
            [this](double fromHz, double toHz) {
                // /filter/notch has add= and clear= and no move, so a move is
                // both, in that order, through the sequencer -- fired together
                // into a threaded server the add could land first and the clear
                // would then delete it.
                QList<ChainPresetWrite> writes;
                writes.append({QStringLiteral("/filter/notch"),
                               QStringLiteral("clear=%1").arg(qint64(fromHz)),
                               tr("take the notch at %1 Hz away")
                                   .arg(groupedHz(qint64(fromHz)))});
                writes.append({QStringLiteral("/filter/notch"),
                               QStringLiteral("add=%1").arg(qint64(toHz)),
                               tr("put one at %1 Hz").arg(groupedHz(qint64(toHz)))});
                emit requestSequence(writes, tr("MOVE NOTCH"));
            });
    connect(m_panel, &DiversityFilterPanel::markClicked, this,
            &AetherGateChainVisual::stageRequested);
    connect(m_panel, &DiversityFilterPanel::cursorMoved, this,
            [this](double hz, double db) {
                if (std::isnan(hz)) {
                    m_cursor->setText(QString());
                    m_cursor->setAccessibleDescription(QString());
                    return;
                }
                const QString text =
                    std::isnan(db)
                        ? tr("%1 Hz").arg(groupedHz(qint64(std::lround(hz))))
                        : tr("%1 Hz · %2 dB")
                              .arg(groupedHz(qint64(std::lround(hz))),
                                   QString::number(db, 'f', 1));
                m_cursor->setText(text);
                m_cursor->setAccessibleDescription(text);
            });

    clear();
}

void AetherGateChainVisual::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    // Coming to the front, the picture catches up with the last body rather
    // than sitting empty until the next poll half a second later.
    if (m_active && !m_last.isEmpty())
        applyFilter(m_last);
}

bool AetherGateChainVisual::dragging() const
{
    return m_panel && m_panel->dragging();
}

void AetherGateChainVisual::clear()
{
    m_last = QJsonObject();
    m_panel->clear();
    m_cursor->setText(QString());
    m_readout->setText(QString());
}

void AetherGateChainVisual::applyFilter(const QJsonObject& filter)
{
    if (filter.isEmpty() || !filter.value(QStringLiteral("error")).toString().isEmpty())
        return;
    m_last = filter;
    // A body arriving while the tab is behind is REMEMBERED and not drawn:
    // parsing it, comparing it and repainting a widget nobody can see is
    // exactly the cost the operator called lag.
    if (!m_active)
        return;
    // A 2 Hz poll landing mid-drag would snatch the handle out from under the
    // pointer, so the picture is fed between gestures and not during one.
    if (m_panel->dragging())
        return;
    const QJsonValue low = filter.value(QStringLiteral("low_hz"));
    const QJsonValue high = filter.value(QStringLiteral("high_hz"));
    if (low.isDouble())
        m_gateLowHz = int(std::lround(low.toDouble()));
    if (high.isDouble())
        m_gateHighHz = int(std::lround(high.toDouble()));
    m_panel->applyStatus(filter);
    refreshReadout();
}

// Which edge moved? The one that is no longer where the GATE said it was. A
// drag of the low handle writes low= alone rather than re-asserting a high=
// the auto-width tracker may own, which would fight the tracker every time an
// operator touched the other edge.
void AetherGateChainVisual::onEdgesDragged(int lowHz, int highHz)
{
    QList<ChainPresetWrite> writes;
    if (lowHz != m_gateLowHz) {
        writes.append({QStringLiteral("/filter/set"),
                       QStringLiteral("low=%1").arg(lowHz),
                       tr("low edge to %1 Hz").arg(groupedHz(lowHz))});
    }
    if (highHz != m_gateHighHz) {
        writes.append({QStringLiteral("/filter/set"),
                       QStringLiteral("high=%1").arg(highHz),
                       tr("high edge to %1 Hz").arg(groupedHz(highHz))});
    }
    if (writes.isEmpty())
        return;
    if (writes.size() == 1) {
        emit requestWrite(writes.first().route, QUrlQuery(writes.first().query));
        return;
    }
    // Both edges at once is the WIDTH presets' case, not a drag's. Two writes
    // in order, each waited for, rather than two GETs racing each other.
    emit requestSequence(writes, tr("PASSBAND"));
}

void AetherGateChainVisual::refreshReadout()
{
    QStringList parts;
    parts << tr("%1-%2 Hz").arg(m_panel->lowHz()).arg(m_panel->highHz());
    const double floorDb = m_panel->spectrumFloorDb();
    // A dash, not a zero: before the gate has heard a block there is no floor,
    // and "0.0 dB" would be a measurement nobody made.
    parts << (std::isnan(floorDb) ? tr("floor —")
                                  : tr("floor %1 dB").arg(floorDb, 0, 'f', 1));
    const int notches = m_panel->notchCount();
    parts << (notches == 1 ? tr("1 notch") : tr("%1 notches").arg(notches));
    if (!std::isnan(m_panel->autoLowHz()) && !std::isnan(m_panel->autoHighHz())) {
        parts << tr("AUTO %1-%2")
                     .arg(qint64(m_panel->autoLowHz()))
                     .arg(qint64(m_panel->autoHighHz()));
    }
    const QString text = parts.join(QStringLiteral(" · "));
    m_readout->setText(text);
    m_readout->setAccessibleDescription(text);
}

} // namespace AetherSDR
