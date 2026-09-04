#include "AetherGateDiversityPanel.h"

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/AetherGateChainAuto.h"
#include "gui/DiversityScope.h"
#include "gui/DiversityWindow.h"
#include "gui/Theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace AetherSDR {

// Row labels resolve their colour from a theme token instead of a literal —
// same {{token}}-through-applyStyleSheet() shape AetherGateApplet.cpp's own
// copy uses for its resolution/device rows (docs/style/theme-style-guide.md;
// this keeps the file off the hardcoded-colour ratchet in static-checks.yml).
static const char* kRowLabelStyle =
    "QLabel { color: {{color.text.secondary}}; font-size: 10px; font-weight: bold; }";

// Persisted open/shut state of the pop-out window, in the "True"/"False"
// AppSettings shape the rest of the app's own boolean keys use. Geometry is
// PersistentDialog's own job, under DiversityWindowGeometry.
static const char* kWindowVisibleKey = "DiversityWindowVisible";

// Opt-in glance-view: the compact scope under the status line. Off by
// default (docs/DIVERSITY-ROADMAP.md §3) and deliberately with no UI to flip
// it -- the window is where the scope is meant to be read.
static const char* kShowScopeKey = "AetherGateDiversityPanel_ShowScope";

// The one control that must read as a control at a glance: a real,
// full-width button rather than a link on a caption.
static const char* kOpenWindowStyle =
    "QPushButton { color: {{color.accent.bright}}; font-size: 11px; font-weight: bold; "
    "padding: 5px 8px; border: 1px solid {{color.accent}}; border-radius: 4px; "
    "background: transparent; }"
    "QPushButton:hover { background: {{color.background.1}}; }"
    "QPushButton:pressed { background: {{color.background.3}}; }";

namespace {

// Everything the sidebar has no number for. Never "0", never "off" -- an
// unmeasured value and a measured zero are different claims (Principle II:
// honest displays). A function rather than a namespace-scope QString: the
// glyph is multi-byte UTF-8, so it has to be built through QStringLiteral
// rather than a QLatin1String over the source bytes.
QString emDash()
{
    return QStringLiteral("—");
}

void styleRowLabel(QLabel* label)
{
    ThemeManager::instance().applyStyleSheet(label, QString::fromLatin1(kRowLabelStyle));
}

// A JSON leg that is absent, null or a non-number is "no estimate", not 0.0.
bool jsonNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

// "+1.4" / "−1.4" -- always signed, and with the true minus sign the rest of
// the diversity text formatting already uses for dB.
QString signedDb(double v)
{
    if (v < 0.0)
        return QStringLiteral("−%1").arg(-v, 0, 'f', 1);
    return QStringLiteral("+%1").arg(v, 0, 'f', 1);
}

// The operator's own label for the live talker, or an empty string when the
// gate carries no memory entry for that id (an older gate has no ids at all).
QString talkerName(const QJsonArray& memory, int id)
{
    for (const QJsonValue& entry : memory) {
        const QJsonObject talker = entry.toObject();
        if (talker.value(QStringLiteral("id")).toInt(-1) != id)
            continue;
        return talker.value(QStringLiteral("name")).toString();
    }
    return QString();
}

// The whole sidebar readout: "track · #3 Bob · +1.4 dB".
//
//   * the mode, as the gate names it -- these are wire values, not phrases,
//     and the mode buttons/combo already carry the translated captions;
//   * who is talking, when the payload says so: "#3" from talker.id, plus
//     the operator's name for that id if memory carries one;
//   * what the combiner is buying over the better single loop:
//     out - max(a, b), signed.
//
// "off" is the whole line when the combiner is off -- there is no talker to
// attribute and no gain to claim. Any leg the gate did not measure is an em
// dash, never an invented zero.
QString statusText(const QJsonObject& d)
{
    const QString mode = d.value(QStringLiteral("mode")).toString();
    if (mode.isEmpty())
        return emDash();
    if (mode == QLatin1String("off"))
        return mode;

    QStringList parts;
    parts << mode;

    const QJsonValue talkerValue = d.value(QStringLiteral("talker"));
    if (talkerValue.isObject()) {
        const int id = talkerValue.toObject().value(QStringLiteral("id")).toInt(-1);
        if (id >= 0) {
            const QString name =
                talkerName(d.value(QStringLiteral("memory")).toArray(), id);
            parts << (name.isEmpty() ? QStringLiteral("#%1").arg(id)
                                     : QStringLiteral("#%1 %2").arg(id).arg(name));
        }
    }

    // The gain is over the BETTER loop, not over A: claiming +3 dB against
    // the loop that was already losing would flatter the combiner. One leg
    // missing is still answerable (compare against the leg there is); no
    // legs, or no combined output, is not.
    const QJsonObject snr = d.value(QStringLiteral("snr_db")).toObject();
    double a = 0.0;
    double b = 0.0;
    double out = 0.0;
    const bool haveA = jsonNumber(snr, "a", &a);
    const bool haveB = jsonNumber(snr, "b", &b);
    if (jsonNumber(snr, "out", &out) && (haveA || haveB)) {
        const double best = haveA ? (haveB ? std::max(a, b) : a) : b;
        parts << QStringLiteral("%1 dB").arg(signedDb(out - best));
    } else {
        parts << emDash();
    }
    return parts.join(QStringLiteral(" · "));
}

// The widest line statusText() can build out of its FIXED parts -- the mode
// word, a four-digit talker id and a signed gain. A long operator name can
// still run past it; the label's Ignored horizontal policy means that costs
// a clipped name rather than a wider sidebar.
QString statusWorstCase()
{
    return QStringLiteral("manual · #9999 · −99.9 dB");
}

} // namespace

AetherGateDiversityPanel::AetherGateDiversityPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gateDiversityBox"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 6, 0, 0);
    root->setSpacing(4);

    // --- the status line ---------------------------------------------------
    m_statusLine = new QLabel(emDash(), this);
    m_statusLine->setObjectName(QStringLiteral("gateDiversityStatusLabel"));
    m_statusLine->setAccessibleName(tr("Diversity status"));
    styleRowLabel(m_statusLine);
    // A minimum sized to the longest FIXED phrase, so switching between them
    // never resizes the label; Ignored horizontally so a long talker name
    // cannot push the sidebar column wider than the applet's own rows.
    m_statusLine->setMinimumWidth(
        m_statusLine->fontMetrics().horizontalAdvance(statusWorstCase()) + 4);
    m_statusLine->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    root->addWidget(m_statusLine);

    // --- the opt-in glance scope -------------------------------------------
    m_scope = new DiversityScope(this);
    m_scope->setVisible(AppSettings::instance()
                            .value(QLatin1String(kShowScopeKey), QStringLiteral("False"))
                            .toString()
                        == QStringLiteral("True"));
    root->addWidget(m_scope);

    // --- the mode selector -------------------------------------------------
    auto* modeRow = new QWidget(this);
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(4);

    auto* modeLabel = new QLabel(tr("Mode"), modeRow);
    styleRowLabel(modeLabel);
    modeLayout->addWidget(modeLabel);

    m_mode = new QComboBox(modeRow);
    m_mode->setObjectName(QStringLiteral("gateDiversityModeCombo"));
    m_mode->setAccessibleName(tr("Diversity combining mode"));
    m_mode->addItem(tr("Off"), QStringLiteral("off"));
    m_mode->addItem(tr("Manual"), QStringLiteral("manual"));
    m_mode->addItem(tr("Null"), QStringLiteral("null"));
    m_mode->addItem(tr("Track"), QStringLiteral("track"));
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx < 0)
            return;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("mode"), m_mode->itemData(idx).toString());
        emit requestSet(q);
    });
    modeLayout->addWidget(m_mode, 1);
    root->addWidget(modeRow);

    // --- AUTO CLEAN: the switch and the indicator, one control -------------
    // "Auto clean should be an option somewhere that we can turn on and off
    // but it should be really visible when we turn that on" -- the operator,
    // verbatim. A checkable button rather than a link: pressed IS on, exactly
    // like DeviceStrip's own DIVERSITY toggle. Not optimistic -- see
    // applyDiversity() below, which is the only place this ever moves once
    // built.
    m_autoCleanButton = new QPushButton(tr("AUTO CLEAN"), this);
    m_autoCleanButton->setObjectName(QStringLiteral("gateDiversityAutoCleanButton"));
    m_autoCleanButton->setAccessibleName(tr("AUTO CLEAN switch"));
    m_autoCleanButton->setCheckable(true);
    m_autoCleanButton->setCursor(Qt::PointingHandCursor);
    // The same emphasised ON tone every other switch in this sidebar wears
    // (U1: the warning gold read as an alarm, and AUTO CLEAN is not one).
    applyToggleButtonStyle(m_autoCleanButton);
    // Same Ignored treatment m_statusLine above already carries. The face
    // text is now a fixed short string ("AUTO CLEAN" / "AUTO CLEAN ON" --
    // see chainAutoSetButtonIndicator()), so nothing here ever needs to
    // elide, but the policy costs nothing to keep.
    m_autoCleanButton->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_autoCleanButton->setMinimumWidth(0);
    m_autoCleanButton->setVisible(false);   // shown once a governor block arrives
    connect(m_autoCleanButton, &QPushButton::clicked, this, [this](bool checked) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("auto"),
                       checked ? QStringLiteral("on") : QStringLiteral("off"));
        emit requestSet(q);
    });
    root->addWidget(m_autoCleanButton);

    // --- the door ----------------------------------------------------------
    m_openWindowButton = new QPushButton(tr("Open Diversity window"), this);
    m_openWindowButton->setObjectName(QStringLiteral("gateDiversityOpenWindowButton"));
    m_openWindowButton->setAccessibleName(tr("Open the diversity window"));
    m_openWindowButton->setToolTip(tr("Everything else about the combiner, in a full "
                                      "window: a large scope, per-antenna meters, the "
                                      "noise map and the remembered stations."));
    m_openWindowButton->setCursor(Qt::PointingHandCursor);
    ThemeManager::instance().applyStyleSheet(m_openWindowButton,
                                             QString::fromLatin1(kOpenWindowStyle));
    connect(m_openWindowButton, &QPushButton::clicked, this,
            &AetherGateDiversityPanel::toggleWindow);
    root->addWidget(m_openWindowButton);

    setVisible(false);   // hidden until a poll reports "available": true
}

// Built once and then kept -- re-creating it on every open would throw away
// the scope's weight trail and the event log. Every write it makes is
// re-emitted as one of THIS panel's own request signals by
// DiversityWindow::createFor(), so the applet's transport wiring never has to
// know the window exists.
DiversityWindow* AetherGateDiversityPanel::window() const
{
    return m_window.data();
}

void AetherGateDiversityPanel::toggleWindow()
{
    if (!m_window) {
        m_window = DiversityWindow::createFor(this);
        m_window->setPresent(m_present);
        // Wired here rather than in createFor(): a signal can only be emitted
        // by the object that owns it, and this is that object.
        connect(m_window, &DiversityWindow::bandPageChanged, this,
                [this] { emit bandPollChanged(); });
        connect(m_window, &DiversityWindow::requestOpenChain, this,
                &AetherGateDiversityPanel::requestOpenChain);
        // The window is built lazily, so the slice frequency the applet has
        // been pushing since it connected has to be handed over once here --
        // otherwise the first BEACON CHECK of a session would have nowhere to
        // come home to until the next poll.
        m_window->setActiveSliceHz(m_activeSliceHz);
    }
    const bool wantVisible = !m_window->isVisible();
    AppSettings::instance().setValue(QLatin1String(kWindowVisibleKey),
                                      wantVisible ? QStringLiteral("True")
                                                  : QStringLiteral("False"));
    if (!wantVisible) {
        m_window->hide();
        return;
    }
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

bool AetherGateDiversityPanel::wantsMapPoll() const
{
    return m_window && m_window->isVisible();
}

bool AetherGateDiversityPanel::wantsBandPoll() const
{
    return m_window && m_window->isVisible() && m_window->bandPageVisible();
}

bool AetherGateDiversityPanel::wantsSitePoll() const
{
    return m_window && m_window->isVisible()
           && (m_window->sitePageVisible() || m_window->beaconPollWanted());
}

bool AetherGateDiversityPanel::wantsFilterPoll() const
{
    return m_window && m_window->isVisible() && m_window->filterPageVisible();
}

void AetherGateDiversityPanel::setPresent(bool present)
{
    m_present = present;
    // Stays OPEN across a gate drop, but every readout in it clears.
    if (m_window)
        m_window->setPresent(present);
    if (present)
        return;

    setVisible(false);
    m_statusLine->setText(emDash());
    m_scope->clear();
    m_autoCleanButton->setVisible(false);
    const QSignalBlocker block(m_autoCleanButton);
    m_autoCleanButton->setChecked(false);
    m_autoCleanButton->setText(tr("AUTO CLEAN"));
}

void AetherGateDiversityPanel::restoreCompareHold()
{
    // Nothing in the sidebar can hold "Hear A only" any more -- see the
    // header comment.
}

void AetherGateDiversityPanel::applyDiversity(const QJsonObject& d, bool isJson)
{
    // The earliest moment reopening the window makes sense -- m_windowRestored.
    if (!m_windowRestored && isJson && d.value(QStringLiteral("available")).toBool()) {
        m_windowRestored = true;
        if (!m_window
            && AppSettings::instance()
                   .value(QLatin1String(kWindowVisibleKey), QStringLiteral("False"))
                   .toString() == QStringLiteral("True")) {
            toggleWindow();
        }
    }
    // Fed unconditionally: the not-JSON and not-available cases are how the
    // window clears itself.
    if (m_window)
        m_window->applyDiversity(d, isJson);

    if (!isJson) {
        setVisible(false);
        return;
    }

    const bool available = d.value(QStringLiteral("available")).toBool();
    setVisible(available);
    if (!available) {
        m_statusLine->setText(emDash());
        m_scope->clear();
        m_autoCleanButton->setVisible(false);
        const QSignalBlocker block(m_autoCleanButton);
        m_autoCleanButton->setChecked(false);
        m_autoCleanButton->setText(tr("AUTO CLEAN"));
        return;
    }

    // AUTO CLEAN's own indicator/switch. The governor block rides on this
    // same /diversity body under "governor" (docs/DIVERSITY.md "AUTO CLEAN:
    // the chain decides") -- no new poll, the existing one just gets read
    // twice. Hidden entirely until a governor block has arrived at all (an
    // older gate); blocked while set so pressing it does not read back
    // whatever this same poll already answered as a second click.
    {
        const ChainAutoGovernor gov = chainAutoParseGovernor(d);
        m_autoCleanButton->setVisible(gov.available);
        const QSignalBlocker block(m_autoCleanButton);
        m_autoCleanButton->setChecked(gov.autoOn);
        chainAutoSetButtonIndicator(m_autoCleanButton, chainAutoIndicatorLine(gov),
                                    chainAutoStateWord(gov));
    }

    // Written from a poll only when the combo is neither focused nor has its
    // popup open — an operator with the dropdown open deciding between Null
    // and Track must not have it snap to whatever the gate currently reports
    // out from under their cursor.
    if (!m_mode->hasFocus() && !m_mode->view()->isVisible()) {
        const QSignalBlocker block(m_mode);
        const int idx = m_mode->findData(d.value(QStringLiteral("mode")).toString());
        if (idx >= 0)
            m_mode->setCurrentIndex(idx);
    }

    m_statusLine->setText(statusText(d));

    // Every poll (and every write's read-back) feeds the scope raw — it does
    // its own defensive field-by-field reading, so an old gate's payload
    // (none of the v2 keys) or a malformed one leaves it painting whatever
    // it already had rather than crashing or inventing zeros. Fed even while
    // AetherGateDiversityPanel_ShowScope keeps it hidden, so turning the key
    // on shows a scope that is already current.
    m_scope->setState(d);
}

void AetherGateDiversityPanel::applyMap(const QJsonObject& map)
{
    if (m_window)
        m_window->applyMap(map);
}

void AetherGateDiversityPanel::applySpatial(const QJsonObject& spatial)
{
    if (m_window)
        m_window->applySpatial(spatial);
}

void AetherGateDiversityPanel::applyFinder(const QJsonObject& finder)
{
    if (m_window)
        m_window->applyFinder(finder);
}

void AetherGateDiversityPanel::applyBeacons(const QJsonObject& beacons)
{
    if (m_window)
        m_window->applyBeacons(beacons);
}

void AetherGateDiversityPanel::applyCompass(const QJsonObject& compass)
{
    if (m_window)
        m_window->applyCompass(compass);
}

void AetherGateDiversityPanel::applyDig(const QJsonObject& dig)
{
    if (m_window)
        m_window->applyDig(dig);
}

void AetherGateDiversityPanel::applyFilter(const QJsonObject& filter)
{
    if (m_window)
        m_window->applyFilter(filter);
}

void AetherGateDiversityPanel::applySiteReply(const QJsonObject& reply)
{
    if (m_window)
        m_window->applySiteReply(reply);
}

void AetherGateDiversityPanel::setActiveSliceHz(double hz)
{
    m_activeSliceHz = hz;
    if (m_window)
        m_window->setActiveSliceHz(hz);
}

void AetherGateDiversityPanel::applyCaptureResult(bool ok, const QString& pathOrError)
{
    if (m_window)
        m_window->applyCaptureResult(ok, pathOrError);
}

} // namespace AetherSDR
